#pragma once

namespace PluginVideoRecord
{
    class ScopedMutexLock
    {
    public:
        explicit ScopedMutexLock(HANDLE mutexHandle)
            : mutexHandle_(mutexHandle)
            , locked_(false)
        {
            if (!mutexHandle_)
            {
                return;
            }

            const DWORD waitResult = WaitForSingleObject(mutexHandle_, INFINITE);
            locked_ = waitResult == WAIT_OBJECT_0 || waitResult == WAIT_ABANDONED;
        }

        ~ScopedMutexLock()
        {
            if (locked_ && mutexHandle_)
            {
                ReleaseMutex(mutexHandle_);
            }
        }

        bool IsLocked() const
        {
            return locked_;
        }

    private:
        HANDLE mutexHandle_;
        bool locked_;
    };
}
