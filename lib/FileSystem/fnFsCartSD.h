#ifndef _FN_FSCARTSD_
#define _FN_FSCARTSD_

#include "fnFS.h"

class CartSDClient;

#ifndef FNIO_IS_STDIO

class FileHandlerCartSD : public FileHandler
{
private:
    CartSDClient &_client;
    uint8_t _handle;
    long _size;
    long _position = 0;
    bool _closed = false;

public:
    FileHandlerCartSD(CartSDClient &client, uint8_t handle, long size);
    ~FileHandlerCartSD() override;

    int close(bool destroy = true) override;
    int seek(long int off, int whence) override;
    long int tell() override;
    size_t read(void *ptr, size_t size, size_t count) override;
    size_t write(const void *ptr, size_t size, size_t count) override;
    int flush() override;
    int eof() override;
};

#endif // !FNIO_IS_STDIO

// Read-only view of the SD card wired to the cartridge MCU, through CartSDClient.
class FileSystemCartSD : public FileSystem
{
private:
    CartSDClient &_client;
    bool _dir_open = false;

public:
    explicit FileSystemCartSD(CartSDClient &client) : _client(client) { _started = true; }

    // Owned by the device that owns the client; fujiHost must not delete it
    bool is_global() override { return true; }

    fsType type() override { return FSTYPE_CARTSD; }
    const char *typestring() override { return type_to_string(FSTYPE_CARTSD); }

    FILE *file_open(const char *path, const char *mode = FILE_READ) override;
#ifndef FNIO_IS_STDIO
    FileHandler *filehandler_open(const char *path, const char *mode = FILE_READ) override;
#endif

    bool exists(const char *path) override;

    success_is_true remove(const char *path) override { RETURN_ERROR_AS_FALSE(); }
    success_is_true rename(const char *pathFrom, const char *pathTo) override
    {
        RETURN_ERROR_AS_FALSE();
    }

    bool is_dir(const char *path) override { return false; }
    success_is_true mkdir(const char *path) override { RETURN_ERROR_AS_FALSE(); }
    success_is_true rmdir(const char *path) override { RETURN_ERROR_AS_FALSE(); }
    bool dir_exists(const char *path) override { return true; }

    success_is_true dir_open(const char *path, const char *pattern, uint16_t diropts) override;
    fsdir_entry_t *dir_read() override;
    void dir_close() override;
    uint16_t dir_tell() override;
    success_is_true dir_seek(uint16_t position) override;
};

#endif // _FN_FSCARTSD_
