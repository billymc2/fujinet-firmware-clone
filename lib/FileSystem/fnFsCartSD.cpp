#include "fnFsCartSD.h"

#include <cerrno>
#include <cstring>
#include <new>

#include "CartSDClient.h"
#include "compat_string.h"
#include "../../include/debug.h"

#ifndef FNIO_IS_STDIO

FileHandlerCartSD::FileHandlerCartSD(CartSDClient &client, uint8_t handle, long size)
    : _client(client), _handle(handle), _size(size)
{
}

FileHandlerCartSD::~FileHandlerCartSD()
{
    if (!_closed)
        _client.close_file(_handle);
}

int FileHandlerCartSD::close(bool destroy)
{
    if (!_closed)
    {
        _closed = true;
        _client.close_file(_handle);
    }
    if (destroy)
        delete this;
    return 0;
}

int FileHandlerCartSD::seek(long int off, int whence)
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

long int FileHandlerCartSD::tell()
{
    return _position;
}

size_t FileHandlerCartSD::read(void *ptr, size_t size, size_t count)
{
    if (_closed || ptr == nullptr || size == 0 || count == 0)
        return 0;

    size_t got = _client.read_file(_handle, static_cast<uint32_t>(_position), ptr, size * count);
    _position += got;
    return got / size;
}

size_t FileHandlerCartSD::write(const void *ptr, size_t size, size_t count)
{
    return 0;
}

int FileHandlerCartSD::flush()
{
    return 0;
}

int FileHandlerCartSD::eof()
{
    return _position >= _size ? 1 : 0;
}

#endif // !FNIO_IS_STDIO

FILE *FileSystemCartSD::file_open(const char *path, const char *mode)
{
    Debug_println("FileSystemCartSD::file_open() unsupported, use filehandler_open()");
    return nullptr;
}

#ifndef FNIO_IS_STDIO
FileHandler *FileSystemCartSD::filehandler_open(const char *path, const char *mode)
{
    // Read-only, so "r+" would accept writes and drop them
    if (mode != nullptr && (mode[0] != 'r' || strpbrk(mode, "+wa") != nullptr))
        return nullptr;

    auto info = _client.open_file(path);
    if (!info)
        return nullptr;

    FileHandler *handler = new (std::nothrow) FileHandlerCartSD(_client, info->handle, info->size);
    if (handler == nullptr)
        _client.close_file(info->handle);
    return handler;
}
#endif

bool FileSystemCartSD::exists(const char *path)
{
    auto info = _client.open_file(path);
    if (!info)
        return false;
    _client.close_file(info->handle);
    return true;
}

success_is_true FileSystemCartSD::dir_open(const char *path, const char *pattern,
                                           uint16_t diropts)
{
    _dir_open = _client.open_directory(path, pattern).is_success();
    RETURN_SUCCESS_IF(_dir_open);
}

fsdir_entry_t *FileSystemCartSD::dir_read()
{
    if (!_dir_open)
        return nullptr;

    auto entry = _client.read_dir_entry();
    if (!entry)
        return nullptr;

    strlcpy(_direntry.filename, entry->name.c_str(), sizeof(_direntry.filename));
    _direntry.isDir = entry->is_dir;
    _direntry.size = 0;
    _direntry.modified_time = 0;
    return &_direntry;
}

void FileSystemCartSD::dir_close()
{
    // fujiHost::cleanup() calls this whether or not a listing is open
    if (!_dir_open)
        return;
    _dir_open = false;
    _client.close_directory();
}

uint16_t FileSystemCartSD::dir_tell()
{
    if (!_dir_open)
        return FNFS_INVALID_DIRPOS;
    return _client.directory_position().value_or(FNFS_INVALID_DIRPOS);
}

success_is_true FileSystemCartSD::dir_seek(uint16_t position)
{
    if (!_dir_open)
        RETURN_ERROR_AS_FALSE();
    return _client.set_directory_position(position);
}
