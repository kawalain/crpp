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
  if (s.repository) {
    assets::WindowsPlatform platform;
    rows.push_back({s.repository->url, short_id(assets::fingerprint(s.repository->key, platform)), true});
  }
  for (const AddedRepository& added : s.added) rows.push_back({added.address, added.id, false});
  s.app.settings().system().set_repositories(std::move(rows));
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
  s.added.push_back({id, typed, assets::Repository{folder, "", fetched.catalog.key}, dir, fetched.catalog});
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
  const auto of_it = [&](const InstallJob& job) { return job.kind == InstallJob::Kind::Pack && job.skin.starts_with(prefix); };
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

void load(Shell& s) {
  s.added.clear();
  assets::WindowsPlatform platform;
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
    const std::string id = short_id(assets::fingerprint(catalog->key, platform));
    if (narrow(dir.filename().wstring()) != id) continue;
    s.added.push_back({id, typed, assets::Repository{folder, "", catalog->key}, dir, std::move(*catalog)});
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
  tab.on_remove_repository = [sp](std::size_t index) {
    // The official one is first and has no button.
    const std::size_t first = sp->repository ? 1 : 0;
    if (index < first || index - first >= sp->added.size()) return;
    const AddedRepository& added = sp->added[index - first];
    sp->app.dialogs().show({locale::fill(tr().repository_remove_question, {added.address}), tr().system_repository_remove, tr().cancel, false,
                            [sp, id = added.id] { remove(*sp, id); }, nullptr});
  };
  show_rows(s);
}

bool scene(Shell& s, std::string_view name) {
  // settings-system-repositories[-failed]: one added, and one being added that could not be.
  if (!name.starts_with("settings-system-repositories")) return false;
  s.app.settings().open(SettingsPage::System);
  s.app.settings().set_version("v0.0.0");
  s.added = {{"1a2b3c4d5e6f7a8b", "github:someone/cloudroom-skins", {}, {}, {}}};
  show_rows(s);
  if (name.ends_with("-failed")) {
    s.app.settings().system().address().set_text(L"https://example.org/skins");
    set_adding(s, SystemTab::Adding::State::Failed, tr().repository_reason_none);
  }
  return true;
}

}  // namespace crpp::gui::area::repositories
