// What happens between the window opening and the Home screen.
#include <cstdio>
#include <random>
#include <vector>

#include "areas.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "crpp/skin/pack.hpp"
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

std::string random_nickname(const files::Tree& assets, std::string_view words) {
  const auto load = [&](const char* name) {
    std::vector<std::string> list;
    std::string text = assets.read(std::string(words) + "/" + name, 1024 * 1024).value_or(std::string());
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
    nickname = random_nickname(official_files(s), locale::current().words_folder);
    if (!first_run) data.set("nickname", store::Json(nickname));
  }
  s.app.home().set_nickname(nickname);
  s.config.server_url = data.string_or("serverUrl", store::default_server_url);
  s.config.nickname = nickname;
  area::room::names_changed(s);
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
  return !groups.empty() && assets::missing(*s.catalog, groups, s.assets_dir, s.files_dir).empty();
}

namespace {

// What an install on that shelf leaves alone: the packs held back.
std::vector<std::string> left_alone(const Shell& s, std::size_t shelf) {
  std::vector<std::string> held;
  for (const Hold& hold : s.holds) {
    const auto at = area::repositories::place(s, hold.parent);
    if (at && at->first == shelf) assets::hold_skin(held, at->second, shelf == 0 && s.catalog ? &*s.catalog : nullptr);
  }
  return held;
}

DiagEvent diag_of(assets::Error error) {
  switch (error) {
    case assets::Error::Network: return DiagEvent::AssetsNetwork;
    case assets::Error::Unavailable: return DiagEvent::AssetsUnavailable;
    case assets::Error::BadContent:
    case assets::Error::Outdated: return DiagEvent::AssetsBadContent;
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
  s.engine->set_sounds(official_sounds(s));
}

struct Installed {
  std::optional<assets::Catalog> catalog;  // a newly fetched one, which the files now follow
  std::optional<assets::Catalog> offered;  // a newly read one, kept for the next start
  std::optional<assets::Error> offered_error;
  std::vector<std::pair<std::string, assets::Catalog>> others;  // the latest of those the user added, by id
  std::vector<assets::Error> other_errors;
  std::vector<Hold> holds;
  std::vector<Candidate> wanted, rekeyed;
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
  if (job.kind == InstallJob::Kind::Pack || job.kind == InstallJob::Kind::Sound) {
    // Each Repository's share in turn, with progress over them all.
    const auto bytes_of = [&s](const InstallPart& part) {
      std::uint64_t bytes = 0;
      for (const assets::File* file : assets::missing(*part.catalog, part.groups, part.dir, s.files_dir)) bytes += file->size;
      return bytes;
    };
    std::uint64_t total = 0, before = 0;
    for (const InstallPart& part : job.parts) total += bytes_of(part);
    for (const InstallPart& part : job.parts) {
      if (cancel) break;
      const std::uint64_t share = bytes_of(part);
      done.result = assets::install(*part.catalog, part.base, part.groups, part.dir, s.files_dir, platform,
                                    [&](const assets::Progress& p) {
                                      assets::Progress all = p;
                                      all.bytes_done += before;
                                      all.bytes = total;
                                      progress(all);
                                    },
                                    &cancel, part.held);
      before += share;
      if (!done.result) break;
    }
    // A Sound pack that failed goes back to the one before, so what arrived of it goes too.
    if (cancel || (job.kind == InstallJob::Kind::Sound && !done.result)) {
      // Not wanted after all: the next start would otherwise finish it behind the app.
      for (const InstallPart& part : job.parts) {
        std::vector<std::string> going;
        for (const std::string& group : part.groups) {
          if (std::ranges::find(part.keep, group) == part.keep.end()) going.push_back(group);
        }
        (void)assets::uninstall(*part.catalog, going, part.dir);
      }
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
  // The latest catalogs of the Repositories the user added too, or else what was kept.
  std::vector<std::optional<assets::Catalog>> latest(job.others.size());
  for (std::size_t i = 0; i < job.others.size(); ++i) {
    const AddedRepository& other = job.others[i];
    auto got = assets::fetch_catalog(other.repository, platform);
    if (got && assets::offers_only_packs(got->catalog) && assets::save_catalog(other.dir, got->text)) latest[i] = std::move(got->catalog);
    else done.other_errors.push_back(got ? assets::Error::BadContent : got.error());
    // Signed by another key now: the same people with a new key, or someone else, which the user decides.
    if (!got && got.error() == assets::Error::BadSignature) {
      if (auto fresh = assets::fetch_new_catalog(other.repository.url, platform)) {
        std::string fingerprint = assets::fingerprint(fresh->catalog.key, platform);
        if (fingerprint != other.fingerprint) done.rekeyed.push_back({other.address, other.repository.url, std::move(*fresh), std::move(fingerprint)});
      }
    }
  }
  std::vector<assets::Shelf> shelves{{job.official_fingerprint, catalog}};
  for (std::size_t i = 0; i < job.others.size(); ++i) {
    const AddedRepository& other = job.others[i];
    shelves.push_back({other.fingerprint, latest[i] ? &*latest[i] : other.catalog ? &*other.catalog : nullptr});
  }
  // A parent whose latest an installed pack's pin does not take stays as it is, until the user
  // takes it anyway in the system tab.
  std::vector<std::vector<std::string>> held(shelves.size());
  held[0] = job.held;
  const auto full = [&](const assets::Chain::Link& link) { return link.shelf == 0 ? link.pack : job.others[link.shelf - 1].id + "/" + link.pack; };
  for (const std::string& skin : job.installed) {
    std::size_t shelf = 0;
    std::string pack = skin;
    if (const std::size_t slash = skin.find('/'); slash != std::string::npos) {
      const auto it = std::ranges::find(job.others, std::string_view(skin).substr(0, slash), &AddedRepository::id);
      if (it == job.others.end()) continue;
      shelf = 1 + static_cast<std::size_t>(it - job.others.begin());
      pack = skin.substr(slash + 1);
    }
    const assets::Chain chain = assets::pack_chain(shelves, 0, shelf, pack);
    for (std::size_t i = 1; i < chain.links.size(); ++i) {
      const assets::Chain::Link& link = chain.links[i];
      const assets::Catalog& parent = *shelves[link.shelf].catalog;
      if (!link.pin || assets::check_pin(*link.pin, parent, link.pack, platform) == assets::PinCheck::Holds) continue;
      const std::string child = full(chain.links[i - 1]);
      // A pack that extends another installed one meets the same link again.
      if (std::ranges::any_of(done.holds, [&](const Hold& hold) { return hold.child == child && hold.parent == full(link); })) continue;
      const auto accepted = std::ranges::find(job.accepted, child, &std::pair<std::string, std::string>::first);
      if (accepted != job.accepted.end() && accepted->second == assets::own_digest(parent, link.pack, platform)) continue;
      done.holds.push_back({full(link), child});
      const assets::Catalog* installed_by = link.shelf == 0 ? (s.catalog ? &*s.catalog : nullptr) : job.others[link.shelf - 1].catalog ? &*job.others[link.shelf - 1].catalog : nullptr;
      assets::hold_skin(held[link.shelf], link.pack, installed_by);
    }
  }
  // A pack in a format this exe does not read stays as it was installed, for a release that reads it.
  assets::hold_unreadable(held[0], *catalog, s.catalog ? &*s.catalog : nullptr, skin::oldest_pack_format, skin::pack_format);
  for (std::size_t i = 0; i < job.others.size(); ++i) {
    if (const assets::Catalog* latest_of = shelves[i + 1].catalog) {
      const assets::Catalog* installed_by = job.others[i].catalog ? &*job.others[i].catalog : nullptr;
      assets::hold_unreadable(held[i + 1], *latest_of, installed_by, skin::oldest_pack_format, skin::pack_format);
    }
  }
  const auto groups = assets::wanted_groups(*catalog, s.assets_dir);
  if (wait || !assets::missing(*catalog, groups, s.assets_dir, s.files_dir).empty()) {
    done.result = assets::install(*catalog, base, groups, s.assets_dir, s.files_dir, platform, progress, &cancel, held[0]);
  }
  // A Repository a pack names but that is not added yet, which could not be read when it was asked
  // for, or which a pack named since.
  {
    std::vector<const assets::Catalog*> from;
    std::vector<std::string> known;
    for (const assets::Shelf& shelf : shelves) {
      known.push_back(shelf.fingerprint);
      if (shelf.catalog) from.push_back(shelf.catalog);
    }
    if (!cancel) done.wanted = area::repositories::read_wanted(std::move(from), std::move(known), nullptr);
  }
  // Then what was picked from the Repositories the user added.
  for (std::size_t i = 0; i < job.others.size(); ++i) {
    const AddedRepository& other = job.others[i];
    const assets::Catalog* follow = shelves[i + 1].catalog;
    if (follow && !cancel) {
      const auto kept = assets::kept_groups(*follow, other.dir);
      if (!assets::missing(*follow, kept, other.dir, s.files_dir).empty()) {
        if (const auto result = assets::install(*follow, other.repository.url, kept, other.dir, s.files_dir, platform, nullptr, &cancel, held[i + 1]); !result) {
          done.other_errors.push_back(result.error());
        }
      }
    }
    if (latest[i]) done.others.emplace_back(other.id, std::move(*latest[i]));
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
  if (job.kind == InstallJob::Kind::Sound) {
    area::sound::pack_fetched(s, job.skin, installed.result);
    return;
  }
  if (job.kind == InstallJob::Kind::Remove) {
    if (!installed.result) s.log.note(diag_of(installed.result.error()));
    area::preferences::skins_installed(s);
    area::sound::packs_changed(s);
    return;
  }
  const bool wait = job.kind == InstallJob::Kind::First;
  if (installed.catalog) {
    s.offered = *installed.catalog;
    use_catalog(s, std::move(*installed.catalog));
  }
  if (installed.offered) s.offered = std::move(*installed.offered);
  if (!wait) {
    s.holds = std::move(installed.holds);
    for (const Hold& hold : s.holds) s.log.note(DiagEvent::UpdateHeld, hold.parent + " " + hold.child);
    area::repositories::found(s, std::move(installed.wanted), std::move(installed.rekeyed));
  }
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
    s.app.reload_assets(official_files(s));
    area::preferences::skins_installed(s);
    enter_app(s);
  } else {
    area::preferences::offer_packs(s);
    area::repositories::show_packs(s);
    area::sound::packs_changed(s);
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
    for (const SkinInfo& info : s.app.ui().skins) {
      job.installed.push_back(info.id);
      if (std::string digest = area::repositories::accepted(s, info.id); !digest.empty()) job.accepted.emplace_back(info.id, std::move(digest));
    }
    job.official_fingerprint = s.official_fingerprint;
  }
  s.install_running = job;
  s.install_cancel = std::make_shared<std::atomic<bool>>(false);
  s.assets_worker = std::thread([&s, job, cancel = s.install_cancel] {
    int shown = -1;
    std::size_t files_shown = 0;
    auto installed = install(s, job, *cancel, [&](const assets::Progress& p) {
      // A redraw for each step the eye can see: a percent, or a file.
      const int percent = p.bytes == 0 ? 100 : static_cast<int>(p.bytes_done * 100 / p.bytes);
      if (job.kind == InstallJob::Kind::Behind || job.kind == InstallJob::Kind::Sound || (percent == shown && p.files_done == files_shown)) return;
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

namespace {

// A pack and what it extends, each Repository's share from its latest catalog. The official packs
// beside a development exe are there already.
InstallJob pack_job(Shell& s, std::span<const assets::Shelf> shelves, const assets::Chain& chain, const std::string& skin) {
  InstallJob job{.kind = InstallJob::Kind::Pack, .skin = skin};
  std::vector<std::size_t> of_part;
  for (const assets::Chain::Link& link : chain.links) {
    if (!area::repositories::installs_into(s, link.shelf)) continue;
    auto it = std::ranges::find(of_part, link.shelf);
    if (it == of_part.end()) {
      of_part.push_back(link.shelf);
      job.parts.push_back({std::make_shared<const assets::Catalog>(*shelves[link.shelf].catalog), area::repositories::base_of(s, link.shelf),
                           area::repositories::install_dir(s, link.shelf), {}, area::repositories::needed_groups(s, shelves, link.shelf),
                           left_alone(s, link.shelf)});
      assets::hold_unreadable(job.parts.back().held, *shelves[link.shelf].catalog, link.shelf == 0 && s.catalog ? &*s.catalog : nullptr,
                              skin::oldest_pack_format, skin::pack_format);
      it = of_part.end() - 1;
    }
    job.parts[static_cast<std::size_t>(it - of_part.begin())].groups.push_back(assets::skin_group(link.pack));
  }
  return job;
}

}  // namespace

void start_install(Shell& s, bool wait) { queue_install(s, {wait ? InstallJob::Kind::First : InstallJob::Kind::Behind, {}}); }

namespace {

// A pack in a chain whose pin on the pack it extends does not hold, and was not accepted as it is.
struct Broken {
  std::string child, parent;  // as settings name them
  std::string digest;         // of the parent now
  assets::PinCheck check;
  const assets::ParentPin* pin;
  const assets::SkinPack* child_pack;
  const assets::SkinPack* parent_pack;
};

std::vector<Broken> broken_pins(const Shell& s, std::span<const assets::Shelf> shelves, const assets::Chain& chain) {
  assets::WindowsPlatform platform;
  std::vector<Broken> broken;
  for (std::size_t i = 1; i < chain.links.size(); ++i) {
    const assets::Chain::Link& link = chain.links[i];
    if (!link.pin) continue;
    const assets::Catalog& catalog = *shelves[link.shelf].catalog;
    const assets::PinCheck check = assets::check_pin(*link.pin, catalog, link.pack, platform);
    if (check == assets::PinCheck::Holds) continue;
    std::string child = area::repositories::full_id(s, chain.links[i - 1].shelf, chain.links[i - 1].pack);
    std::string digest = assets::own_digest(catalog, link.pack, platform);
    if (area::repositories::accepted(s, child) == digest) continue;
    broken.push_back({std::move(child), area::repositories::full_id(s, link.shelf, link.pack), std::move(digest), check, link.pin,
                      shelves[chain.links[i - 1].shelf].catalog->skin(chain.links[i - 1].pack), catalog.skin(link.pack)});
  }
  return broken;
}

// What the user is told of the gravest: content that is not what was pinned, or another version.
std::string pin_question(std::span<const Broken> broken) {
  const auto worst = std::ranges::find(broken, assets::PinCheck::Digest, &Broken::check);
  const Broken& one = worst != broken.end() ? *worst : broken.front();
  const std::string_view locale = locale::current().id;
  const std::string child(name_in(one.child_pack->names, locale, one.child_pack->id));
  const std::string parent(name_in(one.parent_pack->names, locale, one.parent_pack->id));
  if (one.check == assets::PinCheck::Digest) return locale::fill(tr().pin_question_digest, {child, parent});
  if (one.parent_pack->version.empty()) return locale::fill(tr().pin_question_unversioned, {child, parent, one.pin->version});
  return locale::fill(tr().pin_question_version, {child, parent, one.pin->version, one.parent_pack->version});
}

}  // namespace

bool fetch_pack(Shell& s, const std::string& skin) {
  const auto shelves = area::repositories::shelves(s);
  const assets::Chain chain = area::repositories::chain_of(s, shelves, skin);
  if (chain.gap != assets::Chain::Gap::None) return false;
  const auto same = [&](const InstallJob& queued) { return queued.kind == InstallJob::Kind::Pack && queued.skin == skin; };
  if ((s.install_running && same(*s.install_running) && !s.install_cancel->load()) || std::ranges::any_of(s.install_queue, same)) return true;
  // A parent that is no longer what its child was made for: asked first, with cancel as the
  // answer that keeps things as they are.
  if (const auto broken = broken_pins(s, shelves, chain); !broken.empty()) {
    std::vector<std::array<std::string, 3>> accepted;
    for (const Broken& one : broken) accepted.push_back({one.child, one.parent, one.digest});
    Shell* sp = &s;
    s.app.dialogs().show({pin_question(broken), tr().pin_anyway, tr().cancel, true,
                          [sp, skin, accepted] {
                            for (const auto& [child, parent, digest] : accepted) {
                              area::repositories::accept(*sp, child, digest);
                              sp->log.note(DiagEvent::ParentAccepted, child + " " + parent + " " + digest);
                            }
                            if (!fetch_pack(*sp, skin)) area::preferences::pack_fetched(*sp, skin, false, std::unexpected(assets::Error::Unavailable));
                          },
                          [sp, skin] { area::preferences::pack_fetched(*sp, skin, true, {}); }});
    return true;
  }
  queue_install(s, pack_job(s, shelves, chain, skin));
  return true;
}

void remove_pack(Shell& s, const std::string& skin) {
  const auto at = area::repositories::place(s, skin);
  if (!at || !area::repositories::installs_into(s, at->first)) return;
  const auto shelves = area::repositories::shelves(s);
  if (!shelves[at->first].catalog) return;
  queue_install(s, {.kind = InstallJob::Kind::Remove,
                    .skin = skin,
                    .catalog = std::make_shared<const assets::Catalog>(*shelves[at->first].catalog),
                    .dir = area::repositories::install_dir(s, at->first),
                    .groups = {assets::skin_group(at->second)}});
}

bool sound_coming(const Shell& s, std::string_view id) {
  const auto same = [&](const InstallJob& job) { return job.kind == InstallJob::Kind::Sound && job.skin == id; };
  return (s.install_running && same(*s.install_running)) || std::ranges::any_of(s.install_queue, same);
}

bool fetch_sound(Shell& s, const std::string& id) {
  const auto at = area::repositories::place(s, id);
  if (!at || at->first == 0) return false;
  const AddedRepository& added = s.added[at->first - 1];
  if (!added.catalog || !added.catalog->sound(at->second)) return false;
  if (sound_coming(s, id)) return true;
  InstallJob job{.kind = InstallJob::Kind::Sound, .skin = id};
  job.parts.push_back({std::make_shared<const assets::Catalog>(*added.catalog), added.repository.url, added.dir, {assets::sound_group(at->second)}, {}, {}});
  queue_install(s, std::move(job));
  return true;
}

void remove_sound(Shell& s, const std::string& id) {
  const auto at = area::repositories::place(s, id);
  if (!at || at->first == 0) return;
  const AddedRepository& added = s.added[at->first - 1];
  if (!added.catalog) return;
  queue_install(s, {.kind = InstallJob::Kind::Remove,
                    .skin = id,
                    .catalog = std::make_shared<const assets::Catalog>(*added.catalog),
                    .dir = added.dir,
                    .groups = {assets::sound_group(at->second)}});
}

void cancel_pack(Shell& s, const std::string& skin) {
  const auto same = [&](const InstallJob& job) { return job.kind == InstallJob::Kind::Pack && job.skin == skin; };
  std::erase_if(s.install_queue, same);
  if (s.install_running && same(*s.install_running)) *s.install_cancel = true;
}

}  // namespace crpp::gui
