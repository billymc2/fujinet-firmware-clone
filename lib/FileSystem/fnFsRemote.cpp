#include "fnFsRemote.h"

#include <cerrno>
#include <cstring>
#include <new>

#include "compat_string.h"
#include "../../include/debug.h"

#ifndef FNIO_IS_STDIO

FileHandlerRemote::FileHandlerRemote(RemoteStorage &storage,
                                     const RemoteStorage::FileInfo &info)
    : _storage(storage), _handle(info.handle), _size(info.size)
{
}

FileHandlerRemote::~FileHandlerRemote()
{
    if (!_closed)
        _storage.close_file(_handle);
}

int FileHandlerRemote::close(bool destroy)
{
    if (!_closed)
    {
        _closed = true;
        _storage.close_file(_handle);
    }
    if (destroy)
        delete this;
    return 0;
}

int FileHandlerRemote::seek(long int off, int whence)
{
    long pos;
    switch (whence)
    {
    case SEEK_SET:
        pos = off;
        break;
    case SEEK_CUR:
        pos = _position + off;
        break;
    case SEEK_END:
        pos = _size + off;
        break;
    default:
        errno = EINVAL;
        return -1;
    }
    if (pos < 0)
    {
        errno = EINVAL;
        return -1;
    }
    // Every read carries its own offset, so there is nothing to send here
    _position = pos;
    return 0;
}

long int FileHandlerRemote::tell()
{
    return _position;
}

size_t FileHandlerRemote::read(void *ptr, size_t size, size_t n)
{
    if (_closed || ptr == nullptr || size == 0 || n == 0)
        return 0;

    size_t got = _storage.read_file(_handle, (uint32_t)_position, ptr, size * n);
    _position += got;
    return got / size;
}

size_t FileHandlerRemote::write(const void *ptr, size_t size, size_t n)
{
    return 0;
}

int FileHandlerRemote::flush()
{
    return 0;
}

int FileHandlerRemote::eof()
{
    return _position >= _size ? 1 : 0;
}

#endif // !FNIO_IS_STDIO

FILE *FileSystemRemote::file_open(const char *path, const char *mode)
{
    Debug_println("FileSystemRemote::file_open() unsupported, use filehandler_open()");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemRemote::filehandler_open(const char *path, const char *mode)
{
    // Read-only, so "r+" would accept writes and drop them
    if (mode != nullptr && (mode[0] != 'r' || strpbrk(mode, "+wa") != nullptr))
        return nullptr;

    auto info = _storage.open_file(path);
    if (!info)
        return nullptr;

    FileHandler *fh = new (std::nothrow) FileHandlerRemote(_storage, *info);
    if (fh == nullptr)
        _storage.close_file(info->handle);
    return fh;
}
#endif

bool FileSystemRemote::exists(const char *path)
{
    auto info = _storage.open_file(path);
    if (!info)
        return false;
    _storage.close_file(info->handle);
    return true;
}

success_is_true FileSystemRemote::dir_open(const char *path, const char *pattern,
                                           uint16_t diropts)
{
    _dir_open = _storage.open_directory(path, pattern).is_success();
    RETURN_SUCCESS_IF(_dir_open);
}

fsdir_entry_t *FileSystemRemote::dir_read()
{
    if (!_dir_open)
        return nullptr;

    auto entry = _storage.read_dir_entry();
    if (!entry)
        return nullptr;

    strlcpy(_direntry.filename, entry->name.c_str(), sizeof(_direntry.filename));
    _direntry.isDir = entry->is_dir;
    _direntry.size = 0;
    _direntry.modified_time = 0;
    return &_direntry;
}

void FileSystemRemote::dir_close()
{
    // fujiHost::cleanup() calls this whether or not a listing is open
    if (!_dir_open)
        return;
    _dir_open = false;
    _storage.close_directory();
}

uint16_t FileSystemRemote::dir_tell()
{
    if (!_dir_open)
        return FNFS_INVALID_DIRPOS;
    return _storage.directory_position();
}

success_is_true FileSystemRemote::dir_seek(uint16_t position)
{
    if (!_dir_open)
        RETURN_ERROR_AS_FALSE();
    return _storage.set_directory_position(position);
}
