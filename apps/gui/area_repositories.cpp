// Where Skins come from (ADR 0008): the official Repository, which stays, and those the user adds
// in the system tab. Each added one has a folder named by the first digits of its key's
// fingerprint, holding its catalog, the address it was added by and its packs.
#include <algorithm>
#include <array>
#include <fstream>

#include "areas.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::repositories {

namespace fs = std::filesystem;

namespace {

// What the user typed, then the folder it names.
constexpr const wchar_t* address_name = L"address.txt";
// Before a Sound pack's id among the system tab's rows, which are Skins otherwise.
constexpr std::string_view sound_mark = "sound:";

std::string short_id(std::string_view fingerprint) { return std::string(fingerprint.substr(0, assets::short_fingerprint)); }

void show_rows(Shell& s) {
  std::vector<SystemTab::RepositoryRow> rows;
  if (s.repository) rows.push_back({s.repository->url, short_id(s.official_fingerprint), true});
  for (const AddedRepository& added : s.added) rows.push_back({added.address, added.id, false});
  s.app.settings().system().set_repositories(std::move(rows));
}

// Whether an installed pack other than `skin` extends the pack at that place.
const SkinInfo* needing(const Shell& s, std::span<const assets::Shelf> shelves, std::string_view skin, std::size_t shelf, std::string_view pack) {
  for (const SkinInfo& other : s.app.ui().skins) {
    if (other.id == skin) continue;
    const assets::Chain chain = chain_of(s, shelves, other.id);
    for (std::size_t i = 1; i < chain.links.size(); ++i) {
      if (chain.links[i].shelf == shelf && chain.links[i].pack == pack) return &other;
    }
  }
  return nullptr;
}

void set_adding(Shell& s, SystemTab::Adding::State state, std::string reason = {}) {
  s.app.settings().system().set_adding({state, std::move(reason)});
}

const char* reason_of(assets::Error error) {
  switch (error) {
    case assets::Error::Network: return tr().fetch_reason_network;
    case assets::Error::Unavailable: return tr().repository_reason_none;
    case assets::Error::BadContent:
    case assets::Error::BadSignature: return tr().repository_reason_bad;
    case assets::Error::Disk: return tr().fetch_reason_disk;
  }
  return tr().fetch_reason_network;
}

// Four digits at a time, two lines of eight groups.
std::string spaced(std::string_view digits) {
  std::string out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && i % 4 == 0) out += i % 32 == 0 ? '\n' : ' ';
    out += digits[i];
  }
  return out;
}

// Each Skin it offers that this program can draw, with what it takes to fetch.
std::string offer_text(const assets::Catalog& catalog) {
  const std::string_view locale = locale::current().id;
  std::string lines;
  for (const assets::SkinPack& pack : catalog.skins) {
    if (pack.format != skin::pack_format) continue;
    std::vector<std::string> groups;
    for (const std::string& id : assets::skin_chain(catalog, pack.id)) groups.push_back(assets::skin_group(id));
    std::uint64_t bytes = 0;
    for (const assets::File& file : catalog.files) {
      if (std::ranges::find(groups, file.group) != groups.end()) bytes += file.size;
    }
    if (!lines.empty()) lines += '\n';
    lines += locale::fill(tr().repository_add_pack, {std::string(name_in(pack.names, locale, pack.id)), megabytes(bytes)});
  }
  for (const assets::SoundPack& pack : catalog.sounds) {
    std::uint64_t bytes = 0;
    for (const assets::File& file : catalog.files) {
      if (file.group == assets::sound_group(pack.id)) bytes += file.size;
    }
    if (!lines.empty()) lines += '\n';
    lines += locale::fill(tr().repository_add_sound, {std::string(name_in(pack.names, locale, pack.id)), megabytes(bytes)});
  }
  return lines.empty() ? std::string(tr().repository_add_nothing) : lines;
}

bool write_address(const fs::path& dir, const std::string& typed, const std::string& folder) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  std::ofstream out(dir / address_name, std::ios::binary | std::ios::trunc);
  out << typed << '\n' << folder << '\n';
  out.close();
  return static_cast<bool>(out);
}

// What packs, Themes and rows there are now.
void changed(Shell& s) {
  show_rows(s);
  preferences::skins_installed(s);
  sound::packs_changed(s);
}

// The Sound pack id each device uses.
std::array<std::string, 3> sounds_in_use(const Shell& s) {
  return {s.prefs->get(pref::keyboard_sound), s.prefs->get(pref::mouse_sound), s.prefs->get(pref::pen_sound)};
}

bool known(const Shell& s, std::string_view fingerprint) {
  return fingerprint == s.official_fingerprint || std::ranges::any_of(s.added, [&](const AddedRepository& added) { return added.fingerprint == fingerprint; });
}

// Trusted from now on: its catalog and address are kept in a folder of its own.
bool keep(Shell& s, const Candidate& one) {
  if (known(s, one.fingerprint)) return true;
  const std::string id = short_id(one.fingerprint);
  const fs::path dir = s.repositories_dir / fs::path(widen(id));
  if (!assets::save_catalog(dir, one.fetched.text) || !write_address(dir, one.typed, one.folder)) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    return false;
  }
  s.added.push_back({id, one.typed, assets::Repository{one.folder, "", one.fetched.catalog.key}, dir, one.fetched.catalog, one.fingerprint});
  return true;
}

// The user is asked, with its fingerprint, before a Repository is trusted, and with those its packs
// need, which come with it.
void ask_to_add(Shell& s, Candidate first, std::vector<Candidate> wanted, const std::vector<std::string>& unreadable, bool from_field) {
  std::string text = locale::fill(tr().repository_add_question, {first.typed, offer_text(first.fetched.catalog), spaced(first.fingerprint)});
  // The same address under another key: it may be the same people with a new key, or someone else.
  if (std::ranges::any_of(s.added, [&](const AddedRepository& added) { return added.repository.url == first.folder; })) {
    text += "\n\n";
    text += tr().repository_key_changed;
  }
  std::erase_if(wanted, [&](const Candidate& one) { return known(s, one.fingerprint) || one.fingerprint == first.fingerprint; });
  for (const Candidate& one : wanted) {
    text += "\n\n";
    text += locale::fill(tr().repository_add_also, {one.typed, offer_text(one.fetched.catalog), spaced(one.fingerprint)});
  }
  for (const std::string& name : unreadable) {
    text += "\n\n";
    text += locale::fill(tr().repository_add_unreadable, {name});
  }
  Shell* sp = &s;
  auto all = std::make_shared<std::vector<Candidate>>();
  all->push_back(std::move(first));
  for (Candidate& one : wanted) all->push_back(std::move(one));
  s.app.dialogs().show({std::move(text), tr().system_repository_add, tr().cancel, true,
                        [sp, all, from_field] {
                          bool kept = true;
                          for (const Candidate& one : *all) kept = keep(*sp, one) && kept;
                          if (from_field) {
                            if (kept) sp->app.settings().system().clear_address();
                            set_adding(*sp, kept ? SystemTab::Adding::State::None : SystemTab::Adding::State::Failed, kept ? "" : tr().fetch_reason_disk);
                          }
                          changed(*sp);
                        },
                        nullptr});
}

// Its catalog has been read, and those its packs need.
void checked(Shell& s, const std::string& typed, const std::string& folder, std::expected<assets::Fetched, assets::Error> fetched,
             const std::string& fingerprint, std::vector<Candidate> wanted, const std::vector<std::string>& unreadable) {
  if (!fetched) {
    set_adding(s, SystemTab::Adding::State::Failed, reason_of(fetched.error()));
    return;
  }
  if (known(s, fingerprint)) {
    set_adding(s, SystemTab::Adding::State::Failed, tr().repository_reason_known);
    return;
  }
  set_adding(s, SystemTab::Adding::State::None);
  ask_to_add(s, {typed, folder, std::move(*fetched), fingerprint}, std::move(wanted), unreadable, true);
}

void add(Shell& s, std::string typed) {
  // Kept and shown as typed, without what a paste brings around it.
  const auto blank = [](char c) { return c == ' ' || c == '\t'; };
  while (!typed.empty() && blank(typed.back())) typed.pop_back();
  typed.erase(0, std::ranges::find_if_not(typed, blank) - typed.begin());
  const std::string folder = assets::folder_of(typed);
  if (folder.empty()) {
    set_adding(s, SystemTab::Adding::State::Failed, tr().repository_reason_address);
    return;
  }
  if (s.repository_worker.joinable()) return;
  set_adding(s, SystemTab::Adding::State::Checking);
  std::vector<std::string> known{s.official_fingerprint};
  for (const AddedRepository& added : s.added) known.push_back(added.fingerprint);
  s.repository_worker = std::thread([&s, typed, folder, known]() mutable {
    assets::WindowsPlatform platform;
    auto fetched = assets::fetch_new_catalog(folder, platform);
    std::string fingerprint = fetched ? assets::fingerprint(fetched->catalog.key, platform) : std::string();
    // What its packs extend in Repositories not added yet comes in the same question.
    std::vector<Candidate> wanted;
    std::vector<std::string> unreadable;
    if (fetched) {
      known.push_back(fingerprint);
      wanted = read_wanted({&fetched->catalog}, std::move(known), &unreadable);
    }
    s.tasks.post([&s, typed, folder, fetched = std::move(fetched), fingerprint, wanted = std::move(wanted), unreadable]() mutable {
      s.repository_worker.join();
      checked(s, typed, folder, std::move(fetched), fingerprint, std::move(wanted), unreadable);
    });
  });
}

// Its packs go with it, but not the one in use, and not while one is on its way: that one is
// cancelled, and the folder goes once it has stopped.
void remove(Shell& s, const std::string& id) {
  const auto it = std::ranges::find(s.added, id, &AddedRepository::id);
  if (it == s.added.end()) return;
  const std::string prefix = id + "/";
  if (const SkinChoice choice = choice_of(*s.app.ui().prefs, s.app.ui().skins); choice.skin && choice.skin->id.starts_with(prefix)) {
    s.app.toasts().toast(tr().repository_in_use);
    return;
  }
  if (std::ranges::any_of(sounds_in_use(s), [&](const std::string& id) { return id.starts_with(prefix); })) {
    s.app.toasts().toast(tr().repository_sound_in_use);
    return;
  }
  // Nor one whose packs another Repository's installed pack extends.
  const auto all = shelves(s);
  const std::size_t shelf = 1 + static_cast<std::size_t>(it - s.added.begin());
  for (const SkinInfo& info : s.app.ui().skins) {
    if (info.id.starts_with(prefix)) continue;
    const assets::Chain chain = chain_of(s, all, info.id);
    if (std::ranges::any_of(chain.links, [&](const assets::Chain::Link& link) { return link.shelf == shelf; })) {
      s.app.toasts().toast(locale::fill(tr().repository_needed, {std::string(name_in(info.names, locale::current().id, info.id))}));
      return;
    }
  }
  const auto of_it = [&](const InstallJob& job) {
    return (job.kind == InstallJob::Kind::Pack || job.kind == InstallJob::Kind::Remove || job.kind == InstallJob::Kind::Sound) &&
           job.skin.starts_with(prefix);
  };
  std::erase_if(s.install_queue, of_it);
  if (ThemeFetch& fetch = s.app.ui().theme_fetch; fetch.skin.starts_with(prefix)) fetch = {};
  const fs::path dir = it->dir;
  s.added.erase(it);
  if (s.install_running && of_it(*s.install_running)) {
    *s.install_cancel = true;
    s.remove_after_install.push_back(dir);
  } else {
    std::error_code ec;
    fs::remove_all(dir, ec);  // what could not be removed only takes up space, and is no longer offered
  }
  changed(s);
}

}  // namespace

std::vector<assets::Shelf> shelves(const Shell& s) {
  std::vector<assets::Shelf> list;
  list.push_back({s.official_fingerprint, s.offered ? &*s.offered : s.catalog ? &*s.catalog : nullptr});
  for (const AddedRepository& added : s.added) list.push_back({added.fingerprint, added.catalog ? &*added.catalog : nullptr});
  return list;
}

std::string full_id(const Shell& s, std::size_t shelf, std::string_view pack) {
  if (shelf == 0 || shelf > s.added.size()) return std::string(pack);
  return s.added[shelf - 1].id + "/" + std::string(pack);
}

std::optional<std::pair<std::size_t, std::string>> place(const Shell& s, std::string_view skin) {
  const std::size_t slash = skin.find('/');
  if (slash == std::string_view::npos) return std::pair{std::size_t{0}, std::string(skin)};
  const auto it = std::ranges::find(s.added, skin.substr(0, slash), &AddedRepository::id);
  if (it == s.added.end()) return std::nullopt;
  return std::pair{1 + static_cast<std::size_t>(it - s.added.begin()), std::string(skin.substr(slash + 1))};
}

assets::Chain chain_of(const Shell& s, std::span<const assets::Shelf> shelves, std::string_view skin) {
  const auto at = place(s, skin);
  if (!at) return {.gap = assets::Chain::Gap::Missing};
  return assets::pack_chain(shelves, 0, at->first, at->second);
}

bool installs_into(const Shell& s, std::size_t shelf) {
  if (shelf > 0) return shelf <= s.added.size();
  return s.repository && s.skins_dir == s.assets_dir / "skins";
}

std::filesystem::path install_dir(const Shell& s, std::size_t shelf) {
  return shelf == 0 || shelf > s.added.size() ? s.assets_dir : s.added[shelf - 1].dir;
}

std::string base_of(const Shell& s, std::size_t shelf) {
  if (shelf > 0 && shelf <= s.added.size()) return s.added[shelf - 1].repository.url;
  return s.repository ? s.repository->url : std::string();
}

std::vector<std::string> needed_groups(const Shell& s, std::span<const assets::Shelf> shelves, std::size_t shelf) {
  std::vector<std::string> groups;
  const auto add = [&](std::string_view skin) {
    for (const assets::Chain::Link& link : chain_of(s, shelves, skin).links) {
      if (link.shelf == shelf && std::ranges::find(groups, assets::skin_group(link.pack)) == groups.end()) groups.push_back(assets::skin_group(link.pack));
    }
  };
  for (const SkinInfo& info : s.app.ui().skins) add(info.id);
  if (!shelves.empty() && shelves[0].catalog) add(shelves[0].catalog->default_skin);
  return groups;
}

std::vector<Candidate> read_wanted(std::vector<const assets::Catalog*> from, std::vector<std::string> known, std::vector<std::string>* unreadable) {
  assets::WindowsPlatform platform;
  std::vector<Candidate> found;
  found.reserve(assets::max_chain);  // the catalogs read are looked through in turn, by address
  for (std::size_t next = 0; next < from.size(); ++next) {
    for (const assets::SkinPack& pack : from[next]->skins) {
      if (!pack.pin || pack.pin->fingerprint.empty() || std::ranges::find(known, pack.pin->fingerprint) != known.end()) continue;
      const assets::ParentPin& pin = *pack.pin;
      known.push_back(pin.fingerprint);
      const std::string folder = assets::folder_of(pin.repository);
      auto got = folder.empty() || found.size() == assets::max_chain ? std::unexpected(assets::Error::Unavailable) : assets::fetch_new_catalog(folder, platform);
      if (got && assets::fingerprint(got->catalog.key, platform) == pin.fingerprint) {
        found.push_back({pin.repository, folder, std::move(*got), pin.fingerprint});
        from.push_back(&found.back().fetched.catalog);
      } else if (unreadable) {
        unreadable->push_back(pin.repository.empty() ? pin.fingerprint.substr(0, assets::short_fingerprint) : pin.repository);
      }
    }
  }
  return found;
}

void found(Shell& s, std::vector<Candidate> wanted, std::vector<Candidate> rekeyed) {
  bool added = false;
  for (const Candidate& one : wanted) added = keep(s, one) || added;
  if (added) changed(s);
  // Asked about one at a time: another waits for a start after this question.
  std::erase_if(rekeyed, [&](const Candidate& one) { return known(s, one.fingerprint); });
  if (!rekeyed.empty()) ask_to_add(s, std::move(rekeyed.front()), {}, {}, false);
}

namespace {

// A key of this client's own: each pack's id, and the digest of its parent the user accepted.
constexpr std::string_view accepted_key = "acceptedParents";

}  // namespace

std::string accepted(const Shell& s, std::string_view child) {
  const store::Json* all = s.store ? s.store->find(accepted_key) : nullptr;
  const store::Json* one = all ? all->find(child) : nullptr;
  return one && one->as_string() ? std::string(*one->as_string()) : std::string();
}

void accept(Shell& s, std::string_view child, const std::string& digest) {
  const store::Json* all = s.store->find(accepted_key);
  store::Json updated = all && all->as_object() ? *all : store::Json::object();
  updated.set(child, store::Json(digest));
  s.store->set(accepted_key, std::move(updated));
  (void)s.store->save();
}

std::optional<fs::path> parent_dir(const Shell& s, std::string_view id, std::string_view fingerprint, const fs::path& from) {
  const fs::path name(widen(id));
  if (fingerprint.empty()) {
    if (const fs::path beside = from.parent_path() / name; fs::exists(beside / "pack.cskin")) return beside;
    return s.skins_dir / name;
  }
  if (fingerprint == s.official_fingerprint) return s.skins_dir / name;
  const auto it = std::ranges::find(s.added, fingerprint, &AddedRepository::fingerprint);
  if (it == s.added.end()) return std::nullopt;
  return it->dir / "skins" / name;
}

void show_packs(Shell& s) {
  const std::string_view locale = locale::current().id;
  const auto& skins = s.app.ui().skins;
  const SkinChoice choice = choice_of(*s.app.ui().prefs, skins);
  const auto all = shelves(s);
  std::vector<SystemTab::PackRow> rows;
  s.listed_packs.clear();
  for (const SkinInfo& info : skins) {
    const auto at = place(s, info.id);
    if (!at || !installs_into(s, at->first)) continue;
    const assets::Catalog* catalog = all[at->first].catalog;
    if (!catalog || !catalog->skin(at->second)) continue;
    std::uint64_t bytes = 0;
    for (const assets::File& file : catalog->files) {
      if (file.group == assets::skin_group(at->second)) bytes += file.size;
    }
    // Why it stays: in use, the default the next start would fetch again, or what another needs.
    std::string reason;
    const auto defaults = at->first == 0 ? chain_of(s, all, catalog->default_skin).links : std::vector<assets::Chain::Link>{};
    if (choice.skin == &info) {
      reason = tr().system_pack_in_use;
    } else if (std::ranges::any_of(defaults, [&](const assets::Chain::Link& link) { return link.shelf == 0 && link.pack == at->second; })) {
      reason = tr().system_pack_default;
    } else if (const SkinInfo* other = needing(s, all, info.id, at->first, at->second)) {
      reason = locale::fill(tr().system_pack_needed, {std::string(name_in(other->names, locale, other->id))});
    }
    // An update held back says for whom, over the rest, as that is what the user can act on.
    const auto hold = std::ranges::find(s.holds, info.id, &Hold::parent);
    if (hold != s.holds.end()) {
      const auto child = std::ranges::find(skins, hold->child, &SkinInfo::id);
      reason = locale::fill(tr().system_pack_held, {std::string(child != skins.end() ? name_in(child->names, locale, child->id) : hold->child)});
    }
    const std::string where = reason.empty() ? (at->first > 0 ? s.added[at->first - 1].address : std::string(tr().system_pack_official)) : reason;
    rows.push_back({std::string(name_in(info.names, locale, info.id)), locale::fill(tr().system_pack_detail, {megabytes(bytes), where}), reason.empty(),
                    hold != s.holds.end()});
    s.listed_packs.push_back(info.id);
  }
  // Then the Sound packs fetched from the added Repositories; those of the official one all stay.
  const auto in_use = sounds_in_use(s);
  for (const AddedRepository& added : s.added) {
    if (!added.catalog) continue;
    const auto installed = assets::kept_groups(*added.catalog, added.dir);
    for (const assets::SoundPack& pack : added.catalog->sounds) {
      if (std::ranges::find(installed, assets::sound_group(pack.id)) == installed.end()) continue;
      const std::string id = added.id + "/" + pack.id;
      std::uint64_t bytes = 0;
      for (const assets::File& file : added.catalog->files) {
        if (file.group == assets::sound_group(pack.id)) bytes += file.size;
      }
      const bool used = std::ranges::find(in_use, id) != in_use.end();
      rows.push_back({locale::fill(tr().system_pack_sound, {std::string(name_in(pack.names, locale, pack.id))}),
                      locale::fill(tr().system_pack_detail, {megabytes(bytes), used ? std::string(tr().system_pack_in_use) : added.address}), !used});
      s.listed_packs.push_back(std::string(sound_mark) + id);
    }
  }
  s.app.settings().system().set_packs(std::move(rows));
}

void load(Shell& s) {
  s.added.clear();
  assets::WindowsPlatform platform;
  if (s.repository) s.official_fingerprint = assets::fingerprint(s.repository->key, platform);
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(s.repositories_dir, ec)) {
    if (!entry.is_directory(ec)) continue;
    const fs::path dir = entry.path();
    std::ifstream in(dir / address_name, std::ios::binary);
    std::string typed, folder;
    if (!std::getline(in, typed) || !std::getline(in, folder) || folder != assets::folder_of(folder)) continue;
    const auto stated = assets::load_local_catalog(dir);
    if (!stated) continue;
    auto catalog = assets::load_catalog(dir, stated->key, platform);
    // The folder's name is the key it was added with; another key is not the Repository trusted.
    if (!catalog || !assets::offers_only_packs(*catalog)) continue;
    std::string fingerprint = assets::fingerprint(catalog->key, platform);
    const std::string id = short_id(fingerprint);
    if (narrow(dir.filename().wstring()) != id) continue;
    s.added.push_back({id, typed, assets::Repository{folder, "", catalog->key}, dir, std::move(*catalog), std::move(fingerprint)});
  }
  std::ranges::sort(s.added, {}, &AddedRepository::address);
  show_rows(s);
}

std::vector<App::SkinFolder> skin_folders(const Shell& s) {
  std::vector<App::SkinFolder> folders{{s.skins_dir, {}}};
  for (const AddedRepository& added : s.added) folders.push_back({added.dir / "skins", added.id});
  return folders;
}

AddedRepository* of(Shell& s, std::string_view skin) {
  const std::size_t slash = skin.find('/');
  if (slash == std::string_view::npos) return nullptr;
  const auto it = std::ranges::find(s.added, skin.substr(0, slash), &AddedRepository::id);
  return it == s.added.end() ? nullptr : &*it;
}

namespace {

// The user takes a held update anyway: the parent's latest is accepted for each pack held for, and
// fetched now, to be used from the next start.
void update_held(Shell& s, const std::string& parent) {
  const auto all = shelves(s);
  const auto at = place(s, parent);
  if (!at || !all[at->first].catalog) return;
  assets::WindowsPlatform platform;
  const std::string digest = assets::own_digest(*all[at->first].catalog, at->second, platform);
  for (const Hold& hold : s.holds) {
    if (hold.parent != parent) continue;
    accept(s, hold.child, digest);
    s.log.note(DiagEvent::ParentAccepted, hold.child + " " + parent + " " + digest);
  }
  std::erase_if(s.holds, [&](const Hold& hold) { return hold.parent == parent; });
  (void)fetch_pack(s, parent);
  show_packs(s);
}

}  // namespace

void wire(Shell& s) {
  Shell* sp = &s;
  SystemTab& tab = s.app.settings().system();
  tab.on_add_repository = [sp](std::string typed) { add(*sp, typed); };
  tab.on_remove_pack = [sp](std::size_t index) {
    if (index >= sp->listed_packs.size()) return;
    const std::string id = sp->listed_packs[index];
    if (id.starts_with(sound_mark)) {
      const std::string sound = id.substr(sound_mark.size());
      sp->app.dialogs().show({locale::fill(tr().sound_remove_question, {sound::pack_name(*sp, sound)}), tr().system_pack_remove, tr().cancel, false,
                              [sp, sound] { remove_sound(*sp, sound); }, nullptr});
      return;
    }
    const std::string name(sp->app.settings().system().packs()[index].name);
    sp->app.dialogs().show({locale::fill(tr().pack_remove_question, {name}), tr().system_pack_remove, tr().cancel, false,
                            [sp, id] { remove_pack(*sp, id); }, nullptr});
  };
  tab.on_update_pack = [sp](std::size_t index) {
    if (index >= sp->listed_packs.size()) return;
    const std::string parent = sp->listed_packs[index];
    const auto hold = std::ranges::find(sp->holds, parent, &Hold::parent);
    if (hold == sp->holds.end()) return;
    const auto& skins = sp->app.ui().skins;
    const auto child = std::ranges::find(skins, hold->child, &SkinInfo::id);
    const std::string_view locale = locale::current().id;
    const std::string child_name(child != skins.end() ? name_in(child->names, locale, child->id) : hold->child);
    const std::string name(sp->app.settings().system().packs()[index].name);
    sp->app.dialogs().show({locale::fill(tr().pin_update_question, {name, child_name}), tr().system_pack_update, tr().cancel, false,
                            [sp, parent] { update_held(*sp, parent); }, nullptr});
  };
  tab.on_remove_repository = [sp](std::size_t index) {
    // The official one is first and has no button.
    const std::size_t first = sp->repository ? 1 : 0;
    if (index < first || index - first >= sp->added.size()) return;
    const AddedRepository& added = sp->added[index - first];
    sp->app.dialogs().show({locale::fill(tr().repository_remove_question, {added.address}), tr().system_repository_remove, tr().cancel, false,
                            [sp, id = added.id] { remove(*sp, id); }, nullptr});
  };
  show_rows(s);
  show_packs(s);
}

bool scene(Shell& s, std::string_view name) {
  // settings-system-repositories[-failed|-end|-held]: one added with a pack, and one being added that
  // could not be, or the end of the tab, or there an update held back.
  if (!name.starts_with("settings-system-repositories")) return false;
  s.app.settings().open(SettingsPage::System);
  s.app.settings().set_version("v0.0.0");
  s.added = {{"1a2b3c4d5e6f7a8b", "github:someone/cloudroom-skins", {}, {}, {}}};
  show_rows(s);
  s.app.settings().system().set_packs({{"오로라", locale::fill(tr().system_pack_detail, {"2.4 MB", "github:someone/cloudroom-skins"}), true},
                                       {"레트로", locale::fill(tr().system_pack_detail, {"1.1 MB", tr().system_pack_in_use}), false}});
  if (name.ends_with("-held")) {
    // An update of a pack another extends, held back for it.
    auto rows = s.app.settings().system().packs();
    rows.insert(rows.begin(), {"구름", locale::fill(tr().system_pack_detail, {"14.2 MB", locale::fill(tr().system_pack_held, {"오로라"})}), false, true});
    s.app.settings().system().set_packs(std::move(rows));
  }
  if (name.ends_with("-end") || name.ends_with("-held")) s.app.settings().scroll_to(10000);
  if (name.ends_with("-failed")) {
    s.app.settings().system().address().set_text(L"https://example.org/skins");
    set_adding(s, SystemTab::Adding::State::Failed, tr().repository_reason_none);
  }
  return true;
}

}  // namespace crpp::gui::area::repositories
