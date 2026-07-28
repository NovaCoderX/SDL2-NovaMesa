

#include "../../SDL_internal.h"

static FILE* logFile = NULL;

#ifdef SDL_LOGGING_AMIGAOS3
static const char *GetPriorityName(SDL_LogPriority priority)
{
    switch (priority)
    {
        case SDL_LOG_PRIORITY_VERBOSE:  return "VERBOSE";
        case SDL_LOG_PRIORITY_DEBUG:    return "DEBUG";
        case SDL_LOG_PRIORITY_INFO:     return "INFO";
        case SDL_LOG_PRIORITY_WARN:     return "WARN";
        case SDL_LOG_PRIORITY_ERROR:    return "ERROR";
        case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL";
        default:                        return "UNKNOWN";
    }
}

static const char *GetCategoryName(int category)
{
    switch (category)
    {
        case SDL_LOG_CATEGORY_APPLICATION: return "APP";
        case SDL_LOG_CATEGORY_ERROR:       return "ERROR";
        case SDL_LOG_CATEGORY_ASSERT:      return "ASSERT";
        case SDL_LOG_CATEGORY_SYSTEM:      return "SYSTEM";
        case SDL_LOG_CATEGORY_AUDIO:       return "AUDIO";
        case SDL_LOG_CATEGORY_VIDEO:       return "VIDEO";
        case SDL_LOG_CATEGORY_RENDER:      return "RENDER";
        case SDL_LOG_CATEGORY_INPUT:       return "INPUT";
        case SDL_LOG_CATEGORY_TEST:        return "TEST";
        default:                           return "OTHER";
    }
}

static void SDLCALL AmigaLogOutput(
    void *userdata,
    int category,
    SDL_LogPriority priority,
    const char *message)
{
	if (logFile) {
		fprintf(logFile,
				"[%-8s][%-8s] %s\n",
				GetCategoryName(category),
				GetPriorityName(priority),
				message);
		fflush(logFile);
	}
}
#endif

void SDL_OS3_InitLogging(void)
{
#ifdef SDL_LOGGING_AMIGAOS3
	if (!logFile) {
    	logFile = fopen("SDL_log.txt", "w");
        if (logFile) {
        	// No buffering
            setbuf(logFile, NULL);

        	SDL_LogSetOutputFunction(AmigaLogOutput, NULL);
        }
    }

    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);
#endif
}

void SDL_OS3_QuitLogging(void)
{
    if (logFile)
    {
        fclose(logFile);
        logFile = NULL;
    }
}

