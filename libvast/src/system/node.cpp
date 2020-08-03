/******************************************************************************
 *                    _   _____   __________                                  *
 *                   | | / / _ | / __/_  __/     Visibility                   *
 *                   | |/ / __ |_\ \  / /          Across                     *
 *                   |___/_/ |_/___/ /_/       Space and Time                 *
 *                                                                            *
 * This file is part of VAST. It is subject to the license terms in the       *
 * LICENSE file found in the top-level directory of this distribution and at  *
 * http://vast.io/license. No part of VAST, including this file, may be       *
 * copied, modified, propagated, or distributed except according to the terms *
 * contained in the LICENSE file.                                             *
 ******************************************************************************/

#include "vast/system/node.hpp"

#include "vast/concept/parseable/to.hpp"
#include "vast/concept/parseable/vast/endpoint.hpp"
#include "vast/concept/printable/stream.hpp"
#include "vast/concept/printable/to_string.hpp"
#include "vast/concept/printable/vast/json.hpp"
#include "vast/config.hpp"
#include "vast/defaults.hpp"
#include "vast/detail/assert.hpp"
#include "vast/format/csv.hpp"
#include "vast/format/json.hpp"
#include "vast/format/json/suricata.hpp"
#include "vast/format/syslog.hpp"
#include "vast/format/test.hpp"
#include "vast/format/zeek.hpp"
#include "vast/json.hpp"
#include "vast/logger.hpp"
#include "vast/settings.hpp"
#include "vast/system/accountant.hpp"
#include "vast/system/node.hpp"
#include "vast/system/posix_filesystem.hpp"
#include "vast/system/shutdown.hpp"
#include "vast/system/spawn_archive.hpp"
#include "vast/system/spawn_arguments.hpp"
#include "vast/system/spawn_counter.hpp"
#include "vast/system/spawn_eraser.hpp"
#include "vast/system/spawn_explorer.hpp"
#include "vast/system/spawn_exporter.hpp"
#include "vast/system/spawn_importer.hpp"
#include "vast/system/spawn_index.hpp"
#include "vast/system/spawn_node.hpp"
#include "vast/system/spawn_or_connect_to_node.hpp"
#include "vast/system/spawn_pivoter.hpp"
#include "vast/system/spawn_sink.hpp"
#include "vast/system/spawn_source.hpp"
#include "vast/system/spawn_type_registry.hpp"
#include "vast/system/terminate.hpp"
#include "vast/table_slice.hpp"

#if VAST_HAVE_PCAP
#  include "vast/format/pcap.hpp"
#endif

#include <caf/function_view.hpp>
#include <caf/io/middleman.hpp>
#include <caf/settings.hpp>

#include <chrono>
#include <csignal>
#include <fstream>
#include <sstream>

using namespace caf;

namespace vast::system {

namespace {

// Local commands need access to the node actor.
thread_local node_actor* this_node;

// Convenience function for wrapping an error into a CAF message.
auto make_error_msg(ec code, std::string msg) {
  return caf::make_message(make_error(code, std::move(msg)));
}

/// Helper function to determine whether a component can be spawned at most
/// once.
bool is_singleton(std::string_view type) {
  const char* singletons[]
    = {"accountant", "archive", "eraser",       "filesystem",
       "importer",   "index",   "type-registry"};
  auto pred = [&](const char* x) { return x == type; };
  return std::any_of(std::begin(singletons), std::end(singletons), pred);
}

// Sends an atom to a registered actor. Blocks until the actor responds.
caf::message send_command(const invocation& inv, caf::actor_system& sys) {
  auto first = inv.arguments.begin();
  auto last = inv.arguments.end();
  // Expect exactly two arguments.
  if (std::distance(first, last) != 2)
    return make_error_msg(ec::syntax_error, "expected two arguments: receiver "
                                            "and message atom");
  // Get destination actor from the registry.
  auto dst = self->state.registry.find_by_label(*first);
  if (dst == nullptr)
    return make_error_msg(ec::syntax_error,
                          "registry contains no actor named " + *first);
  // Dispatch to destination.
  auto f = caf::make_function_view(caf::actor_cast<caf::actor>(dst));
  if (auto res = f(caf::atom_from_string(*(first + 1))))
    return std::move(*res);
  else
    return caf::make_message(std::move(res.error()));
}

void collect_component_status(node_actor* self,
                              caf::response_promise status_promise) {
  // Shared state between our response handlers.
  // TODO: we no longer use the key in this settings object; it's always the
  // same node name. So we could simplify the whole structure a bit.
  struct req_state_t {
    // Keeps track of how many requests are pending.
    size_t pending = 0;
    // Promise to the original client request.
    caf::response_promise rp;
    // Maps nodes to a map associating components with status information.
    caf::settings content;
  };
  auto req_state = std::make_shared<req_state_t>();
  req_state->rp = std::move(status_promise);
  // Pre-fill our result with system stats.
  auto& sys_stats = put_dictionary(req_state->content, "system");
  auto& sys = self->system();
  put(sys_stats, "running-actors", sys.registry().running());
  put(sys_stats, "detached-actors", sys.detached_actors());
  put(sys_stats, "worker-threads", sys.scheduler().num_workers());
  put(sys_stats, "table-slices", table_slice::instances());
  // Send out requests and collects answers.
  req_state->pending += self->state.registry.components().size();
  for (auto& [label, component] : self->state.registry.components())
    self
      ->request(component.actor, defaults::system::initial_request_timeout,
                atom::status_v)
      .then(
        [=, lab = label](caf::config_value::dictionary& xs) mutable {
          auto& dict = req_state->content[self->state.name].as_dictionary();
          dict.emplace(std::move(lab), std::move(xs));
          if (--req_state->pending == 0)
            req_state->rp.deliver(to_string(to_json(req_state->content)));
        },
        [=, lab = label](caf::error& err) mutable {
          VAST_WARNING(self, "failed to retrieve", lab,
                       "status:", to_string(err));
          auto& dict = req_state->content[self->state.name].as_dictionary();
          dict.emplace(std::move(lab), to_string(err));
          if (--req_state->pending == 0)
            req_state->rp.deliver(to_string(to_json(req_state->content)));
        });
}

} // namespace

caf::message status_command(const invocation&, caf::actor_system&) {
  auto self = this_node;
  collect_component_status(self, self->make_response_promise());
  return caf::none;
}

maybe_actor spawn_accountant(node_actor* self, spawn_arguments&) {
  auto acc = self->spawn(accountant);
  // FIXME: do not use CAF actor system registry for our components.
  self->system().registry().put(atom::accountant_v,
                                caf::actor_cast<caf::actor>(acc));
  return caf::actor_cast<caf::actor>(acc);
}

caf::expected<caf::actor>
spawn_component(const invocation& inv, spawn_arguments& args) {
  VAST_TRACE(VAST_ARG(inv), VAST_ARG(args));
  using caf::atom_uint;
  auto self = this_node;
  auto i = node_state::component_factory.find(inv.full_name);
  if (i == node_state::component_factory.end())
    return make_error(ec::unspecified, "invalid spawn component");
  VAST_VERBOSE(__func__, VAST_ARG(inv.full_name));
  return i->second(self, args);
}

caf::message kill_command(const invocation& inv, caf::actor_system&) {
  auto first = inv.arguments.begin();
  auto last = inv.arguments.end();
  if (std::distance(first, last) != 1)
    return make_error_msg(ec::syntax_error, "expected exactly one component "
                                            "argument");
  auto rp = this_node->make_response_promise();
  auto& label = *first;
  auto component = this_node->state.registry.find_by_label(label);
  if (!component) {
    rp.deliver(make_error(ec::unspecified, "no such component: " + label));
  } else {
    this_node->demonitor(component);
    terminate<policy::parallel>(this_node, component)
      .then(
        [=](atom::done) mutable {
          VAST_DEBUG(this_node, "terminated component", label);
          rp.deliver(atom::ok_v);
        },
        [=](const caf::error& err) mutable {
          VAST_DEBUG(this_node, "terminated component", label);
          rp.deliver(err);
        });
  }
  return caf::none;
}

/// Lifts a factory function that accepts `local_actor*` as first argument
/// to a function accpeting `node_actor*` instead.
template <maybe_actor (*Fun)(local_actor*, spawn_arguments&)>
node_state::component_factory_fun lift_component_factory() {
  return [](node_actor* self, spawn_arguments& args) {
    // Delegate to lifted function.
    return Fun(self, args);
  };
}

template <maybe_actor (*Fun)(node_actor*, spawn_arguments&)>
node_state::component_factory_fun lift_component_factory() {
  return Fun;
}

auto make_component_factory() {
  return node_state::named_component_factory {
    {"spawn accountant", lift_component_factory<spawn_accountant>()},
      {"spawn archive", lift_component_factory<spawn_archive>()},
      {"spawn counter", lift_component_factory<spawn_counter>()},
      {"spawn eraser", lift_component_factory<spawn_eraser>()},
      {"spawn exporter", lift_component_factory<spawn_exporter>()},
      {"spawn explorer", lift_component_factory<spawn_explorer>()},
      {"spawn importer", lift_component_factory<spawn_importer>()},
      {"spawn type-registry", lift_component_factory<spawn_type_registry>()},
      {"spawn index", lift_component_factory<spawn_index>()},
      {"spawn pivoter", lift_component_factory<spawn_pivoter>()},
      {"spawn source csv",
       lift_component_factory<
         spawn_source<format::csv::reader, defaults::import::csv>>()},
      {"spawn source json",
       lift_component_factory<
         spawn_source<format::json::reader<>, defaults::import::json>>()},
#if VAST_HAVE_PCAP
      {"spawn source pcap",
       lift_component_factory<
         spawn_source<format::pcap::reader, defaults::import::pcap>>()},
#endif
      {"spawn source suricata",
       lift_component_factory<
         spawn_source<format::json::reader<format::json::suricata>,
                      defaults::import::suricata>>()},
      {"spawn source syslog",
       lift_component_factory<
         spawn_source<format::syslog::reader, defaults::import::syslog>>()},
      {"spawn source test",
       lift_component_factory<
         spawn_source<format::test::reader, defaults::import::test>>()},
      {"spawn source zeek",
       lift_component_factory<
         spawn_source<format::zeek::reader, defaults::import::zeek>>()},
      {"spawn sink pcap", lift_component_factory<spawn_pcap_sink>()},
      {"spawn sink zeek", lift_component_factory<spawn_zeek_sink>()},
      {"spawn sink csv", lift_component_factory<spawn_csv_sink>()},
      {"spawn sink ascii", lift_component_factory<spawn_ascii_sink>()},
      {"spawn sink json", lift_component_factory<spawn_json_sink>()},
  };
}

auto make_command_factory() {
  // When updating this list, remember to update its counterpart in
  // application.cpp as well iff necessary
  return command::factory{
    {"kill", kill_command},
    {"send", send_command},
    {"spawn accountant", node_state::spawn_command},
    {"spawn archive", node_state::spawn_command},
    {"spawn counter", node_state::spawn_command},
    {"spawn eraser", node_state::spawn_command},
    {"spawn explorer", node_state::spawn_command},
    {"spawn exporter", node_state::spawn_command},
    {"spawn importer", node_state::spawn_command},
    {"spawn type-registry", node_state::spawn_command},
    {"spawn index", node_state::spawn_command},
    {"spawn pivoter", node_state::spawn_command},
    {"spawn sink ascii", node_state::spawn_command},
    {"spawn sink csv", node_state::spawn_command},
    {"spawn sink json", node_state::spawn_command},
    {"spawn sink pcap", node_state::spawn_command},
    {"spawn sink zeek", node_state::spawn_command},
    {"spawn source csv", node_state::spawn_command},
    {"spawn source json", node_state::spawn_command},
    {"spawn source pcap", node_state::spawn_command},
    {"spawn source suricata", node_state::spawn_command},
    {"spawn source syslog", node_state::spawn_command},
    {"spawn source test", node_state::spawn_command},
    {"spawn source zeek", node_state::spawn_command},
    {"status", status_command},
  };
}

std::string generate_label(node_actor* self, std::string_view component) {
  auto n = self->state.registry.find_by_type(component).size() + 1;
  return std::string{component} + '-' + std::to_string(n);
}

caf::message
node_state::spawn_command(const invocation& inv,
                          [[maybe_unused]] caf::actor_system& sys) {
  VAST_TRACE(inv);
  auto rp = this_node->make_response_promise();
  using std::begin;
  using std::end;
  // We configured the command to have the name of the component.
  auto inv_name_split = detail::split(inv.full_name, " ");
  VAST_ASSERT(inv_name_split.size() > 1);
  std::string comp_type{inv_name_split[1]};
  // Auto-generate label if none given.
  std::string label;
  if (auto label_ptr = caf::get_if<std::string>(&inv.options, "spawn.label")) {
    label = *label_ptr;
    if (this_node->state.registry.find_by_label(label)) {
      auto err = caf::make_error(ec::unspecified, "duplicate component label");
      rp.deliver(err);
      return caf::make_message(std::move(err));
    }
  } else {
    label = comp_type;
    if (!is_singleton(comp_type)) {
      label = generate_label(this_node, comp_type);
      VAST_DEBUG(this_node, "auto-generated new label:", label);
    }
  }
  VAST_DEBUG(this_node, "spawns a", comp_type, "with the label", label);
  auto spawn_inv = inv;
  if (comp_type == "source") {
    auto spawn_opt = caf::get_or(spawn_inv.options, "spawn", caf::settings{});
    auto source_opt = caf::get_or(spawn_opt, "source", caf::settings{});
    auto import_opt = caf::get_or(spawn_inv.options, "import", caf::settings{});
    merge_settings(source_opt, import_opt);
    spawn_inv.options["import"] = import_opt;
  }
  // Spawn our new VAST component.
  spawn_arguments args{spawn_inv, this_node->state.dir, label};
  auto component = spawn_component(spawn_inv, args);
  if (!component) {
    if (component.error())
      VAST_WARNING(__func__,
                   "failed to spawn component:", to_string(component.error()));
    rp.deliver(component.error());
    return caf::make_message(component.error());
  }
  this_node->monitor(*component);
  auto okay = this_node->state.registry.add(*component, std::move(comp_type),
                                            std::move(label));
  VAST_ASSERT(okay);
  rp.deliver(*component);
  return caf::none;
}

caf::behavior node(node_actor* self, std::string name, path dir) {
  self->state.name = std::move(name);
  self->state.dir = std::move(dir);
  // Initialize component and command factories.
  node_state::component_factory = make_component_factory();
  if (node_state::extra_component_factory != nullptr) {
    auto extra = node_state::extra_component_factory();
    // FIXME replace with std::map::merge once CI is updated to a newer libc++
    extra.insert(node_state::component_factory.begin(),
                 node_state::component_factory.end());
    node_state::component_factory = std::move(extra);
  }
  node_state::command_factory = make_command_factory();
  if (node_state::extra_command_factory != nullptr) {
    auto extra = node_state::extra_command_factory();
    // FIXME replace with std::map::merge once CI is updated to a newer libc++
    extra.insert(node_state::command_factory.begin(),
                 node_state::command_factory.end());
    node_state::command_factory = std::move(extra);
  }
  // Initialize the file system with the node directory as root.
  auto fs = self->spawn<linked + detached>(posix_filesystem, self->state.dir);
  self->system().registry().put(atom::filesystem_v, fs);
  // Remove monitored components.
  self->set_down_handler([=](const down_msg& msg) {
    VAST_DEBUG(self, "got DOWN from", msg.source);
    auto component = caf::actor_cast<caf::actor>(msg.source);
    self->state.registry.remove(component);
  });
  // Terminate deterministically on shutdown.
  self->set_exit_handler([=](const exit_msg& msg) {
    self->attach_functor([=](const caf::error&) {
      VAST_DEBUG(self, "terminated all dependent actors");
      // Clean up. This is important for detached actors, otherwise they
      // cause some destructor to hang. (This is a CAF bug; for details see
      // https://github.com/actor-framework/actor-framework/issues/1110.)
      self->system().registry().erase(atom::accountant_v); // FIXME: remove
      self->system().registry().erase(atom::filesystem_v);
    });
    VAST_DEBUG(self, "got EXIT from", msg.source);
    /// Collect all actors that we shutdown in sequence. First, we terminate
    /// the accountant because it acts like a source and flush buffered data.
    /// Thereafter, we tear down the ingestion pipeline from source to sink.
    /// Finally, after everything has exited, we can terminate the filesystem.
    std::vector<caf::actor> actors;
    auto sequence
      = {"accountant", "source", "importer", "archive", "index", "exporter"};
    auto& registry = self->state.registry;
    for (auto type : sequence) {
      for (auto& component : registry.find_by_type(type)) {
        self->demonitor(component);
        registry.remove(component);
        actors.push_back(component);
      }
    }
    // Add remaining components.
    for ([[maybe_unused]] auto& [_, comp] : registry.components()) {
      self->demonitor(comp.actor);
      actors.push_back(comp.actor);
    }
    auto fs = self->system().registry().get(atom::filesystem_v);
    VAST_ASSERT(fs);
    actors.push_back(caf::actor_cast<caf::actor>(fs));
    shutdown<policy::sequential>(self, std::move(actors));
  });
  // Define the node behavior.
  return {
    [=](const invocation& inv) {
      VAST_DEBUG(self, "got command", inv.full_name, "with options",
                 inv.options, "and arguments", inv.arguments);
      // Run the command.
      this_node = self;
      return run(inv, self->system(), node_state::command_factory);
    },
    [=](atom::put, const actor& component,
        const std::string& type) -> result<atom::ok> {
      VAST_DEBUG(self, "got new", type);
      // Check if the new component is a singleton.
      auto& registry = self->state.registry;
      if (is_singleton(type) && registry.find_by_label(type))
        return make_error(ec::unspecified, "component already exists");
      // Generate label
      auto label = generate_label(self, type);
      VAST_DEBUG(self, "generated new component label", label);
      if (!registry.add(component, type, label))
        return make_error(ec::unspecified, "failed to add component");
      self->monitor(component);
      return atom::ok_v;
    },
    [=](atom::get, atom::type, const std::string& type) {
      return self->state.registry.find_by_type(type);
    },
    [=](atom::get, atom::label, const std::string& label) {
      return self->state.registry.find_by_label(label);
    },
    [=](atom::get, atom::label, const std::vector<std::string>& labels) {
      std::vector<caf::actor> result;
      result.reserve(labels.size());
      for (auto& label : labels)
        result.push_back(self->state.registry.find_by_label(label));
      return result;
    },
    [=](atom::signal, int signal) {
      VAST_IGNORE_UNUSED(signal);
      VAST_WARNING(self, "got signal", ::strsignal(signal));
    },
  };
}

} // namespace vast::system
