#ifndef _MAIN_COMPLETION_PATH_H
#define _MAIN_COMPLETION_PATH_H

#include "main/completion.h"

/**
 * Create a new path completion provider.
 *
 * This provider completes filesystem paths.
 * @returns A filesystem path @ref completion_provider.
 */
struct completion_provider create_path_provider(void (*on_complete_path)(void));

#endif
