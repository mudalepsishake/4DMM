/* 3DMMEx:
 * Platform-specific definitions
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <mutex>
#include <condition_variable>
#include <chrono>

/** 3DMMEx: **************************************
    Mutex (critical section) object
****************************************/
typedef class MUTX *PMUTX;

class MUTX : public std::recursive_mutex
{
  public:
    void Enter(void)
    {
        lock();
    }
    void Leave(void)
    {
        unlock();
    }
};

/** 3DMMEx: **************************************
    Signal - simulates a Win32 Event
****************************************/

#define KSIGNAL_INFINITE 0xffffffff

class Signal
{
  public:
    // 3DMMEx: Wake up a thread that is waiting for this event
    void Set()
    {
        {
            std::lock_guard<std::mutex> lock(_mutx);
            _fSignaled = true;
        }

        _cv.notify_one();
    }

    // 3DMMEx: Wait for the event to be signalled
    bool Wait(uint32_t dtsTime = KSIGNAL_INFINITE)
    {
        std::unique_lock<std::mutex> lock(_mutx);

        if (dtsTime == KSIGNAL_INFINITE)
        {
            // 3DMMEx: Wait forever
            _cv.wait(lock, [&] { return _fSignaled; });
            _fSignaled = false;
            return true;
        }
        else
        {
            // 3DMMEx: Wait for the given number of milliseconds
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(dtsTime);
            bool fResult = _cv.wait_until(lock, deadline, [&] { return _fSignaled; });
            if (fResult)
            {
                _fSignaled = false;
            }
            return fResult;
        }
    }

  private:
    std::mutex _mutx;
    std::condition_variable _cv;
    bool _fSignaled = false;
};

/** 3DMMEx: *************************************************************************
    Universal scalable application clock and other time stuff
***************************************************************************/

extern const uint32_t kdtsSecond;
extern uint32_t TsCurrentSystem(void);
extern uint32_t DtsCaret(void);

/** 3DMMEx: *************************************************************************
    Return a path to a directory to store configuration files
***************************************************************************/
extern bool FGetAppConfigDir(char *psz, int32_t cchMax);

/** 3DMMEx: *************************************************************************
    Return a path to a directory to store documents
***************************************************************************/
extern bool FGetDocumentsDir(char *psz, int32_t cchMax);

#ifndef WIN
/** 3DMMEx: **************************************
    Current executable name
****************************************/
extern void GetExecutableName(char *psz, int cchMax);

/** 3DMMEx: **************************************
    Get environment variable
****************************************/
extern uint32_t GetEnvironmentVariable(const char *pcszName, char *pszValue, uint32_t cchMax);

/** 3DMMEx: **************************************
    Current username
****************************************/
extern bool GetUserName(char *psz, int cchMax);

/** 3DMMEx: **************************************
    Directory containing app resources
****************************************/
extern bool FGetResourcesDir(char *psz, int32_t cchMax);

#endif

#endif //! 3DMMEx: PLATFORM_H
