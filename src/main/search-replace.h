struct commands;

/**
 * Abort a replace currently in progress.
 */
void abort_replace();

/**
 * Register search and replace commands
 *
 * @param [in] commands Command registry to register search and
 * replace commands in.
 */
void register_search_replace_commands(struct commands *commands);
