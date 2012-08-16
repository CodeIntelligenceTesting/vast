#ifndef VAST_QUERY_CLIENT_H
#define VAST_QUERY_CLIENT_H

#include <cppa/cppa.hpp>

namespace vast {

/// A simple query client.
class query_client : public cppa::sb_actor<query_client>
{
  friend class cppa::sb_actor<query_client>;

public:
  /// Spawns a query client.
  ///
  /// @param search The search actor.
  ///
  /// @param expression The query expression.
  ///
  /// @param batch_size The number of results to display until requiring user
  /// input.
  query_client(cppa::actor_ptr search,
               std::string const& expression,
               unsigned batch_size);

private:
  void set_batch_size();
  void set_expression();

  std::string expression_;
  unsigned batch_size_ = 0;

  cppa::actor_ptr search_;
  cppa::actor_ptr query_;
  cppa::behavior init_state;
};

} // namespace vast

#endif
