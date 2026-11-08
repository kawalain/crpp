// Where Skins come from (ADR 0008): the official Repository, which stays, and those the user adds
// in the system tab. Each added one has a folder named by the first digits of its key's
// fingerprint, holding its catalog, the address it was added by and its packs.
#include <algorithm>
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
}

void keep(Shell& s, const std::string& typed, const std::string& folder, const assets::Fetched& fetched, const std::string& id) {
  const fs::path dir = s.repositories_dir / fs::path(widen(id));
  if (!assets::save_catalog(dir, fetched.text) || !write_address(dir, typed, folder)) {
    std::error_code ec;
    fs::remove_all(dir, ec);
    set_adding(s, SystemTab::Adding::State::Failed, tr().fetch_reason_disk);
    return;
  }
  assets::WindowsPlatform platform;
  s.added.push_back({id, typed, assets::Repository{folder, "", fetched.catalog.key}, dir, fetched.catalog, assets::fingerprint(fetched.catalog.key, platform)});
  s.app.settings().system().clear_address();
  set_adding(s, SystemTab::Adding::State::None);
  changed(s);
}

// Its catalog has been read: the user is asked, with its fingerprint, before it is trusted.
void checked(Shell& s, const std::string& typed, const std::string& folder, std::expected<assets::Fetched, assets::Error> fetched,
             const std::string& fingerprint) {
  if (!fetched) {
    set_adding(s, SystemTab::Adding::State::Failed, reason_of(fetched.error()));
    return;
  }
  const std::string id = short_id(fingerprint);
  const bool official = s.repository && s.repository->key == fetched->catalog.key;
  if (official || std::ranges::any_of(s.added, [&](const AddedRepository& added) { return added.id == id; })) {
    set_adding(s, SystemTab::Adding::State::Failed, tr().repository_reason_known);
    return;
  }
  std::string text = locale::fill(tr().repository_add_question, {typed, offer_text(fetched->catalog), spaced(fingerprint)});
  // The same address under another key: it may be the same people with a new key, or someone else.
  if (std::ranges::any_of(s.added, [&](const AddedRepository& added) { return added.repository.url == folder; })) {
    text += "\n\n";
    text += tr().repository_key_changed;
  }
  set_adding(s, SystemTab::Adding::State::None);
  Shell* sp = &s;
  auto kept = std::make_shared<assets::Fetched>(std::move(*fetched));
  s.app.dialogs().show({std::move(text), tr().system_repository_add, tr().cancel, true,
                        [sp, typed, folder, kept, id] { keep(*sp, typed, folder, *kept, id); }, nullptr});
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
  s.repository_worker = std::thread([&s, typed, folder] {
    assets::WindowsPlatform platform;
    auto fetched = assets::fetch_new_catalog(folder, platform);
    std::string fingerprint = fetched ? assets::fingerprint(fetched->catalog.key, platform) : std::string();
    s.tasks.post([&s, typed, folder, fetched = std::move(fetched), fingerprint]() mutable {
      s.repository_worker.join();
      checked(s, typed, folder, std::move(fetched), fingerprint);
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
    return (job.kind == InstallJob::Kind::Pack || job.kind == InstallJob::Kind::Remove) && job.skin.starts_with(prefix);
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
    const std::string where = reason.empty() ? (at->first > 0 ? s.added[at->first - 1].address : std::string(tr().system_pack_official)) : reason;
    rows.push_back({std::string(name_in(info.names, locale, info.id)), locale::fill(tr().system_pack_detail, {megabytes(bytes), where}), reason.empty()});
    s.listed_packs.push_back(info.id);
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

void wire(Shell& s) {
  Shell* sp = &s;
  SystemTab& tab = s.app.settings().system();
  tab.on_add_repository = [sp](std::string typed) { add(*sp, typed); };
  tab.on_remove_pack = [sp](std::size_t index) {
    if (index >= sp->listed_packs.size()) return;
    const std::string id = sp->listed_packs[index];
    const std::string name(sp->app.settings().system().packs()[index].name);
    sp->app.dialogs().show({locale::fill(tr().pack_remove_question, {name}), tr().system_pack_remove, tr().cancel, false,
                            [sp, id] { remove_pack(*sp, id); }, nullptr});
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
  // settings-system-repositories[-failed|-end]: one added with a pack, and one being added that
  // could not be, or the end of the tab.
  if (!name.starts_with("settings-system-repositories")) return false;
  s.app.settings().open(SettingsPage::System);
  s.app.settings().set_version("v0.0.0");
  s.added = {{"1a2b3c4d5e6f7a8b", "github:someone/cloudroom-skins", {}, {}, {}}};
  show_rows(s);
  s.app.settings().system().set_packs({{"오로라", locale::fill(tr().system_pack_detail, {"2.4 MB", "github:someone/cloudroom-skins"}), true},
                                       {"레트로", locale::fill(tr().system_pack_detail, {"1.1 MB", tr().system_pack_in_use}), false}});
  if (name.ends_with("-end")) s.app.settings().scroll_to(10000);
  if (name.ends_with("-failed")) {
    s.app.settings().system().address().set_text(L"https://example.org/skins");
    set_adding(s, SystemTab::Adding::State::Failed, tr().repository_reason_none);
  }
  return true;
}

}  // namespace crpp::gui::area::repositories
