#ifndef _FN_FSREMOTE_
#define _FN_FSREMOTE_

#include "fnFS.h"

#include <optional>
#include <string>

// Storage on another device, which a bus device reaches for FileSystemRemote.
class RemoteStorage
{
public:
    struct DirEntry
    {
        std::string name;
        bool is_dir;
    };

    struct FileInfo
    {
        uint8_t handle;
        uint32_t size;
    };

    virtual ~RemoteStorage() = default;

    virtual success_is_true open_directory(const char *path, const char *pattern) = 0;
    // nullopt at the end of the listing or on error
    virtual std::optional<DirEntry> read_dir_entry() = 0;
    virtual void close_directory() = 0;
    // FNFS_INVALID_DIRPOS on error
    virtual uint16_t directory_position() = 0;
    virtual success_is_true set_directory_position(uint16_t pos) = 0;

    virtual std::optional<FileInfo> open_file(const char *path) = 0;
    // Bytes read; short at EOF or on error
    virtual size_t read_file(uint8_t handle, uint32_t offset, void *buf, size_t len) = 0;
    virtual void close_file(uint8_t handle) = 0;
};

#ifndef FNIO_IS_STDIO

class FileHandlerRemote : public FileHandler
{
private:
    RemoteStorage &_storage;
    uint8_t _handle;
    long _size;
    long _position = 0;
    bool _closed = false;

public:
    FileHandlerRemote(RemoteStorage &storage, const RemoteStorage::FileInfo &info);
    ~FileHandlerRemote() override;

    int close(bool destroy = true) override;
    int seek(long int off, int whence) override;
    long int tell() override;
    size_t read(void *ptr, size_t size, size_t n) override;
    size_t write(const void *ptr, size_t size, size_t n) override;
    int flush() override;
    int eof() override;
};

#endif // !FNIO_IS_STDIO

// Read-only view of a RemoteStorage, e.g. an SD card wired to a cartridge MCU.
class FileSystemRemote : public FileSystem
{
private:
    RemoteStorage &_storage;
    bool _dir_open = false;

public:
    explicit FileSystemRemote(RemoteStorage &storage) : _storage(storage) { _started = true; }

    // Owned by the device that owns the storage; fujiHost must not delete it
    bool is_global() override { return true; }

    fsType type() override { return FSTYPE_REMOTE; }
    const char *typestring() override { return type_to_string(FSTYPE_REMOTE); }

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

#endif // _FN_FSREMOTE_
