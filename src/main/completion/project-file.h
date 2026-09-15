#ifndef _MAIN_COMPLETION_PROJECT_FILE_H
#define _MAIN_COMPLETION_PROJECT_FILE_H

#include "main/completion.h"

struct reactor;

/**
 * Create a new file-in-project completion provider.
 *
 * This provider completes files in the project.
 * @returns A project file @ref completion_provider.
 */
struct completion_provider
create_project_file_provider(struct reactor *, void (*on_complete_path)(void));

#endif
