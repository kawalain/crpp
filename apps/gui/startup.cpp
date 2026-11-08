// What happens between the window opening and the Home screen.
#include <cstdio>
#include <random>
#include <vector>

#include "areas.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

// --- moving away from the Shared store ---

void show_cleanup_offer(Shell& s) {
  s.app.notice().set(Notice{tr().notice_moved, tr().notice_erase, tr().notice_keep});
  s.app.notice().on_action = [&s] {
    s.app.notice().set(Notice{s.store->erase_shared_secrets() ? tr().notice_erased
                                                              : tr().notice_erase_failed,
                              "", tr().notice_close});
  };
}

void show_store_notice(Shell& s) {
  if (s.store->mode() == store::Mode::Shared) {
    s.app.notice().set(Notice{tr().notice_shared, tr().notice_move, tr().notice_close});
    s.app.notice().on_action = [&s] {
      if (s.store->convert_to_protected()) {
        show_cleanup_offer(s);
      } else {
        s.app.notice().set(Notice{tr().notice_move_failed, "", tr().notice_close});
      }
    };
  } else if (s.store->secrets_lost()) {
    s.app.notice().set(Notice{tr().notice_unreadable, "", tr().notice_close});
  } else if (s.store->data_corrupt()) {
    s.app.notice().set(Notice{tr().notice_corrupt, "", tr().notice_close});
  }
}

// What the last run left: a Room it was still in, or a recently left Seat.
void restore_seat(Shell& s) {
  const auto resume = s.store->resume();
  if (!resume) return;
  if (!resume->left_at) {
    s.bridge->expect_resume(resume->code);
    s.client->resume(resume->code, resume->key,
                     resume->ticket.empty() ? std::nullopt : std::optional<std::string_view>(resume->ticket), false);
    return;
  }
  const auto now = unix_ms();
  if (now - *resume->left_at < store::left_grace_ms) {
    s.store->remember_seat(resume->code, resume->key, false, true, *resume->left_at);
  }
  s.store->clear_resume();
}

}  // namespace

std::string random_nickname(const std::filesystem::path& words) {
  const auto load = [&](const char* name) {
    std::vector<std::string> list;
    FILE* f = std::fopen((words / name).string().c_str(), "rb");
    if (!f) return list;
    std::string text;
    char buf[512];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    for (std::size_t start = 0, comma = 0; comma != std::string::npos; start = comma + 1) {
      comma = text.find(',', start);
      if (const auto word = text.substr(start, comma - start); !word.empty()) list.push_back(word);
    }
    return list;
  };
  const auto adjectives = load("adjectives.txt"), animals = load("animals.txt");
  if (adjectives.empty() || animals.empty()) return tr().guest;
  std::random_device rd;
  return adjectives[rd() % adjectives.size()] + " " + animals[rd() % animals.size()];
}

void start_client(Shell& s) {
  s.client.reset();  // before the transport it uses
  s.transport = std::make_unique<transport::WinHttpTransport>();
  s.client = std::make_unique<client::Client>(*s.transport, s.config, *s.bridge);
  s.client->start();
  const std::string gid = s.store->string_or("gid", "");
  if (!gid.empty()) s.client->restore_gid(gid);
  const std::string emoji = s.store->string_or("emoji", "");
  session::event::SetSounds sounds;
  sounds.key = s.audio.keyboard_pack;
  sounds.mouse = s.audio.mouse_pack;
  sounds.pen = s.audio.pen_pack;
  if (!emoji.empty()) sounds.emoji = emoji;
  s.client->set_sounds(sounds);
}

// The part of startup that needs the assets in place.
void enter_app(Shell& s) {
  store::Store& data = *s.store;
  // First: it decides on what is stored, before the default below is.
  const bool first_run = area::account::begin_first_run(s);
  std::string nickname = data.string_or("nickname", "");
  if (const std::string handle = data.string_or("handle", ""); nickname.empty() && !handle.empty()) {
    nickname = "@" + handle;  // an account can go by its handle alone
  } else if (nickname.empty()) {
    nickname = random_nickname(s.assets_dir / locale::current().words_folder);
    if (!first_run) data.set("nickname", store::Json(nickname));
  }
  s.app.home().set_nickname(nickname);
  s.config.server_url = data.string_or("serverUrl", store::default_server_url);
  s.config.nickname = nickname;
  s.config.token = data.string_or("token", "");
  show_store_notice(s);
  if (!first_run) s.app.show_home();
  start_client(s);
  if (!s.smoke_path.empty()) {
    s.smoke_start = clock_ms();
    s.client->create(false, false);
    SetTimer(s.hwnd, smoke_timer, 200, nullptr);
  } else {
    // As upstream: nothing is resumed until the server is known to be there.
    area::connection::check_server(s, [&s] {
      restore_seat(s);
      (void)s.store->save();
    });
  }
  (void)data.save();
}

bool skin_installed(const Shell& s) {
  if (!s.catalog) return false;
  std::vector<std::string> groups;
  for (const std::string& id : assets::skin_chain(*s.catalog, s.catalog->default_skin)) groups.push_back(assets::skin_group(id));
  return !groups.empty() && assets::missing(*s.catalog, groups, s.assets_dir).empty();
}

namespace {

DiagEvent diag_of(assets::Error error) {
  switch (error) {
    case assets::Error::Network: return DiagEvent::AssetsNetwork;
    case assets::Error::Unavailable: return DiagEvent::AssetsUnavailable;
    case assets::Error::BadContent: return DiagEvent::AssetsBadContent;
    case assets::Error::BadSignature: return DiagEvent::AssetsBadSignature;
    case assets::Error::Disk: return DiagEvent::AssetsDisk;
  }
  return DiagEvent::AssetsNetwork;
}

// Said once when the app falls back on the system's font and emoji, not at every start of
// a network that keeps the files out.
void report_assets(Shell& s, const std::expected<void, assets::Error>& result) {
  constexpr const char* key = "assetsFailed";
  if (result) {
    s.store->set(key, store::Json(false));
    return;
  }
  s.log.note(diag_of(result.error()));
  if (!s.store->bool_or(key, false)) {
    s.app.ui().toasts->toast(tr().notice_fetch_failed);
    s.store->set(key, store::Json(true));
  }
}

// What a newly installed catalog brings: the emoji to pick, the sounds to expect, the packs.
void use_catalog(Shell& s, assets::Catalog catalog) {
  s.catalog = std::move(catalog);
  s.app.ui().emoji_ids = assets::picker_emoji(*s.catalog);
  s.expected_sounds = assets::sound_files(*s.catalog);
  s.engine->set_expected(s.expected_sounds);
}

struct Installed {
  std::optional<assets::Catalog> catalog;  // a newly fetched one, which the files now follow
  std::optional<assets::Catalog> offered;  // a newly read one, kept for the next start
  std::optional<assets::Error> offered_error;
  std::vector<std::pair<std::string, assets::Catalog>> others;  // the latest of those the user added, by id
  std::vector<assets::Error> other_errors;
  std::expected<void, assets::Error> result;
};

// On the worker thread. A first start fetches the catalog and follows it. A later one reads the
// latest and keeps it for the start after, and meanwhile fetches what is missing of its own: a
// file that is about to change may be open, as a font is. It reads the latest catalogs of the
// Repositories the user added too. A pack picked in the display tab comes from the latest of its
// Repository, with what it extends.
Installed install(Shell& s, const InstallJob& job, const std::atomic<bool>& cancel,
                  const std::function<void(const assets::Progress&)>& progress) {
  assets::WindowsPlatform platform;
  Installed done;
  if (job.kind == InstallJob::Kind::Remove) {
    done.result = assets::uninstall(*job.catalog, job.groups, job.dir);
    return done;
  }
  if (job.kind == InstallJob::Kind::Pack) {
    const std::string one[] = {job.pack};
    const auto groups = chain_groups(*job.catalog, one);
    done.result = assets::install(*job.catalog, job.base, groups, job.dir, platform, progress, &cancel);
    if (cancel) {
      // Not wanted after all: the next start would otherwise finish it behind the app.
      std::vector<std::string> going;
      for (const std::string& group : groups) {
        if (std::ranges::find(job.groups, group) == job.groups.end()) going.push_back(group);
      }
      (void)assets::uninstall(*job.catalog, going, job.dir);
    }
    return done;
  }
  std::string base = s.repository->url;
  const bool wait = job.kind == InstallJob::Kind::First;
  const assets::Catalog* catalog = s.catalog ? &*s.catalog : nullptr;
  auto fetched = assets::fetch_catalog(*s.repository, platform);
  if (fetched && !assets::save_catalog(s.assets_dir, fetched->text)) fetched = std::unexpected(assets::Error::Disk);
  if (fetched && fetched->from_mirror) base.clear();  // the Repository did not answer: its files would not either
  if (!wait) {
    if (fetched) done.offered = std::move(fetched->catalog);
    else done.offered_error = fetched.error();
    if (!catalog) return done;
  } else {
    if (!fetched) {
      done.result = std::unexpected(fetched.error());
      return done;
    }
    done.catalog = std::move(fetched->catalog);
    catalog = &*done.catalog;
  }
  // Behind the app, what is installed follows the latest catalog now, and is used from the next
  // start; an open font waits for a start that does not open it.
  if (!wait && done.offered) catalog = &*done.offered;
  const auto groups = assets::wanted_groups(*catalog, s.assets_dir);
  if (wait || !assets::missing(*catalog, groups, s.assets_dir).empty()) {
    done.result = assets::install(*catalog, base, groups, s.assets_dir, platform, progress, &cancel, job.held);
  }
  // Then what was picked from the Repositories the user added, from their latest, or else from
  // what was kept.
  for (const AddedRepository& other : job.others) {
    auto fetched = assets::fetch_catalog(other.repository, platform);
    std::optional<assets::Catalog> latest;
    if (fetched && assets::offers_only_packs(fetched->catalog) && assets::save_catalog(other.dir, fetched->text)) latest = std::move(fetched->catalog);
    else done.other_errors.push_back(fetched ? assets::Error::BadContent : fetched.error());
    const assets::Catalog* follow = latest ? &*latest : other.catalog ? &*other.catalog : nullptr;
    if (follow && !cancel) {
      const auto kept = assets::kept_groups(*follow, other.dir);
      if (!assets::missing(*follow, kept, other.dir).empty()) {
        if (const auto result = assets::install(*follow, other.repository.url, kept, other.dir, platform, nullptr, &cancel); !result) {
          done.other_errors.push_back(result.error());
        }
      }
    }
    if (latest) done.others.emplace_back(other.id, std::move(*latest));
  }
  return done;
}

// On the UI thread, once the worker has ended.
void finish_install(Shell& s, const InstallJob& job, bool cancelled, Installed installed) {
  // A Repository removed while its pack was coming goes now that nothing writes there.
  for (const std::filesystem::path& dir : std::exchange(s.remove_after_install, {})) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
  }
  for (auto& [id, catalog] : installed.others) {
    if (AddedRepository* added = area::repositories::of(s, id + "/")) added->catalog = std::move(catalog);
  }
  for (const assets::Error error : installed.other_errors) s.log.note(diag_of(error));
  if (job.kind == InstallJob::Kind::Pack) {
    area::preferences::pack_fetched(s, job.skin, cancelled, installed.result);
    return;
  }
  if (job.kind == InstallJob::Kind::Remove) {
    if (!installed.result) s.log.note(diag_of(installed.result.error()));
    area::preferences::skins_installed(s);
    return;
  }
  const bool wait = job.kind == InstallJob::Kind::First;
  if (installed.catalog) {
    s.offered = *installed.catalog;
    use_catalog(s, std::move(*installed.catalog));
  }
  if (installed.offered) s.offered = std::move(*installed.offered);
  // The kept one stands in, as at the last start; why is for the diagnostic log only.
  if (installed.offered_error) s.log.note(diag_of(*installed.offered_error));
  if (wait) {
    if (!skin_installed(s)) {
      s.log.note(diag_of(installed.result ? assets::Error::BadContent : installed.result.error()));
      auto status = s.app.setup().status();
      status.failed = true;
      s.app.show_setup(status);
      return;
    }
    s.app.reload_assets(s.assets_dir);
    area::preferences::skins_installed(s);
    enter_app(s);
  } else {
    area::preferences::offer_packs(s);
  }
  report_assets(s, installed.result);
}

void run_install(Shell& s, InstallJob job) {
  if (job.kind == InstallJob::Kind::First) {
    s.app.show_setup({});
    s.app.setup().on_retry = [&s] {
      if (!s.assets_worker.joinable() && s.install_queue.empty()) start_install(s, true);
    };
    s.app.setup().on_quit = [&s] { DestroyWindow(s.hwnd); };
  }
  if (job.kind == InstallJob::Kind::Behind) {
    job.others = s.added;  // as they are now; the worker reads only its own
    if (s.fonts_loaded && s.catalog) {
      for (const assets::File& file : s.catalog->files) {
        if (file.path.ends_with(".ttf")) job.held.push_back(file.path);
      }
    }
  }
  s.install_running = job;
  s.install_cancel = std::make_shared<std::atomic<bool>>(false);
  s.assets_worker = std::thread([&s, job, cancel = s.install_cancel] {
    int shown = -1;
    std::size_t files_shown = 0;
    auto installed = install(s, job, *cancel, [&](const assets::Progress& p) {
      // A redraw for each step the eye can see: a percent, or a file.
      const int percent = p.bytes == 0 ? 100 : static_cast<int>(p.bytes_done * 100 / p.bytes);
      if (job.kind == InstallJob::Kind::Behind || (percent == shown && p.files_done == files_shown)) return;
      shown = percent;
      files_shown = p.files_done;
      if (job.kind == InstallJob::Kind::First) {
        s.tasks.post([&s, p] { s.app.show_setup({p.files_done, p.files, p.bytes_done, p.bytes}); });
      } else {
        s.tasks.post([&s, skin = job.skin, percent] { area::preferences::pack_progress(s, skin, percent); });
      }
    });
    s.tasks.post([&s, job, cancel, installed = std::move(installed)]() mutable {
      s.assets_worker.join();
      s.install_running.reset();
      finish_install(s, job, cancel->load(), std::move(installed));
      if (s.install_queue.empty() || s.assets_worker.joinable()) return;
      InstallJob next = std::move(s.install_queue.front());
      s.install_queue.pop_front();
      run_install(s, std::move(next));
    });
  });
}

}  // namespace

// One install at a time: a second thread would race the first over the same files, and
// assigning to a running std::thread ends the program.
void queue_install(Shell& s, InstallJob job) {
  if (s.assets_worker.joinable() || !s.install_queue.empty()) {
    s.install_queue.push_back(std::move(job));
    return;
  }
  run_install(s, std::move(job));
}

std::vector<std::string> chain_groups(const assets::Catalog& catalog, std::span<const std::string> packs) {
  std::vector<std::string> groups;
  for (const std::string& pack : packs) {
    for (const std::string& id : assets::skin_chain(catalog, pack)) {
      if (std::ranges::find(groups, assets::skin_group(id)) == groups.end()) groups.push_back(assets::skin_group(id));
    }
  }
  return groups;
}

namespace {

// Where a pack of this id comes from and goes: an added Repository's folder, or the official one's.
// False when no catalog there offers it.
bool locate(Shell& s, const std::string& skin, InstallJob& job) {
  if (AddedRepository* other = area::repositories::of(s, skin)) {
    if (!other->catalog) return false;
    job.catalog = std::make_shared<const assets::Catalog>(*other->catalog);
    job.base = other->repository.url;
    job.dir = other->dir;
    job.pack = skin.substr(other->id.size() + 1);
  } else {
    if (!s.repository || !s.offered || skin.find('/') != std::string::npos) return false;
    job.catalog = std::make_shared<const assets::Catalog>(*s.offered);
    job.base = s.repository->url;
    job.dir = s.assets_dir;
    job.pack = skin;
  }
  return !assets::skin_chain(*job.catalog, job.pack).empty();
}

// The packs installed from the same Repository as this one, by their ids there.
std::vector<std::string> installed_beside(Shell& s, const std::string& skin) {
  const std::size_t slash = skin.find('/');
  const std::string prefix = slash == std::string::npos ? std::string() : skin.substr(0, slash + 1);
  std::vector<std::string> ids;
  for (const SkinInfo& info : s.app.ui().skins) {
    const bool same = prefix.empty() ? info.id.find('/') == std::string::npos : info.id.starts_with(prefix);
    if (same) ids.push_back(info.id.substr(prefix.size()));
  }
  return ids;
}

}  // namespace

void start_install(Shell& s, bool wait) { queue_install(s, {wait ? InstallJob::Kind::First : InstallJob::Kind::Behind, {}}); }

bool fetch_pack(Shell& s, const std::string& skin) {
  InstallJob job{.kind = InstallJob::Kind::Pack, .skin = skin};
  if (!locate(s, skin, job)) return false;
  auto needed = installed_beside(s, skin);
  if (job.dir == s.assets_dir) needed.push_back(job.catalog->default_skin);
  job.groups = chain_groups(*job.catalog, needed);
  const auto same = [&](const InstallJob& queued) { return queued.kind == InstallJob::Kind::Pack && queued.skin == skin; };
  if ((s.install_running && same(*s.install_running) && !s.install_cancel->load()) || std::ranges::any_of(s.install_queue, same)) return true;
  queue_install(s, std::move(job));
  return true;
}

void remove_pack(Shell& s, const std::string& skin) {
  InstallJob job{.kind = InstallJob::Kind::Remove, .skin = skin};
  if (!locate(s, skin, job)) return;
  job.groups = {assets::skin_group(job.pack)};
  queue_install(s, std::move(job));
}

void cancel_pack(Shell& s, const std::string& skin) {
  const auto same = [&](const InstallJob& job) { return job.kind == InstallJob::Kind::Pack && job.skin == skin; };
  std::erase_if(s.install_queue, same);
  if (s.install_running && same(*s.install_running)) *s.install_cancel = true;
}

}  // namespace crpp::gui
