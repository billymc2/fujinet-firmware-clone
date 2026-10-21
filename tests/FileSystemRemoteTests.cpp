// lib/FileSystem/fnFsRemote: FileSystemRemote over a fake RemoteStorage.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fnFsRemote.h"

class FakeStorage : public RemoteStorage
{
public:
    std::vector<DirEntry> listing;
    std::map<std::string, std::string> files;
    std::set<uint8_t> open_handles;
    size_t cursor = 0;
    int dir_closes = 0;
    uint8_t next_handle = 0;

    success_is_true open_directory(const char *path, const char *pattern) override
    {
        cursor = 0;
        RETURN_SUCCESS_IF(std::string(path) == "/");
    }
    std::optional<DirEntry> read_dir_entry() override
    {
        if (cursor >= listing.size())
            return std::nullopt;
        return listing[cursor++];
    }
    void close_directory() override { dir_closes++; }
    uint16_t directory_position() override { return (uint16_t)cursor; }
    success_is_true set_directory_position(uint16_t pos) override
    {
        if (pos > listing.size())
            RETURN_ERROR_AS_FALSE();
        cursor = pos;
        RETURN_SUCCESS_AS_TRUE();
    }

    std::optional<FileInfo> open_file(const char *path) override
    {
        auto it = files.find(path);
        if (it == files.end())
            return std::nullopt;
        uint8_t handle = next_handle++;
        open_handles.insert(handle);
        handles[handle] = path;
        return FileInfo{handle, (uint32_t)it->second.size()};
    }
    size_t read_file(uint8_t handle, uint32_t offset, void *buf, size_t len) override
    {
        const std::string &data = files[handles[handle]];
        if (offset >= data.size())
            return 0;
        size_t n = std::min(len, data.size() - offset);
        memcpy(buf, data.data() + offset, n);
        return n;
    }
    void close_file(uint8_t handle) override { open_handles.erase(handle); }

private:
    std::map<uint8_t, std::string> handles;
};

TEST_CASE("directory listing passes names and directory flags through")
{
    FakeStorage storage;
    storage.listing = {{"GAMES", true}, {"Astrosmash.rom", false}};
    FileSystemRemote fs(storage);

    CHECK(fs.dir_open("/", nullptr, 0).is_success());
    fsdir_entry_t *e = fs.dir_read();
    REQUIRE(e != nullptr);
    CHECK(std::string(e->filename) == "GAMES");
    CHECK(e->isDir);
    e = fs.dir_read();
    REQUIRE(e != nullptr);
    CHECK(std::string(e->filename) == "Astrosmash.rom");
    CHECK_FALSE(e->isDir);
    CHECK(fs.dir_read() == nullptr);

    CHECK(fs.dir_seek(1).is_success());
    CHECK(fs.dir_tell() == 1);
    fs.dir_close();
    CHECK(storage.dir_closes == 1);
}

TEST_CASE("nothing reaches the storage for a directory that is not open")
{
    FakeStorage storage;
    FileSystemRemote fs(storage);

    CHECK(fs.dir_open("/missing", nullptr, 0).is_error());
    CHECK(fs.dir_read() == nullptr);
    CHECK(fs.dir_tell() == FNFS_INVALID_DIRPOS);
    CHECK(fs.dir_seek(0).is_error());
    fs.dir_close();
    CHECK(storage.dir_closes == 0);
}

TEST_CASE("files read, seek and close through their handle")
{
    FakeStorage storage;
    storage.files["/a.rom"] = "0123456789";
    FileSystemRemote fs(storage);

    FileHandler *fh = fs.filehandler_open("/a.rom", "rb");
    REQUIRE(fh != nullptr);
    char buf[16] = {};
    CHECK(fh->read(buf, 1, 4) == 4);
    CHECK(std::string(buf, 4) == "0123");
    CHECK(fh->seek(-2, SEEK_END) == 0);
    CHECK(fh->read(buf, 1, sizeof(buf)) == 2);
    CHECK(std::string(buf, 2) == "89");
    CHECK(fh->eof() == 1);
    CHECK(storage.open_handles.size() == 1);
    fh->close();
    CHECK(storage.open_handles.empty());
}

TEST_CASE("a handler deleted without close() still releases its handle")
{
    FakeStorage storage;
    storage.files["/a.rom"] = "x";
    FileSystemRemote fs(storage);

    FileHandler *fh = fs.filehandler_open("/a.rom", "rb");
    REQUIRE(fh != nullptr);
    delete fh;
    CHECK(storage.open_handles.empty());
}

TEST_CASE("write modes are refused and exists() leaves no handle open")
{
    FakeStorage storage;
    storage.files["/a.rom"] = "x";
    FileSystemRemote fs(storage);

    CHECK(fs.filehandler_open("/a.rom", "r+b") == nullptr);
    CHECK(fs.filehandler_open("/a.rom", "wb") == nullptr);
    CHECK(fs.exists("/a.rom"));
    CHECK_FALSE(fs.exists("/b.rom"));
    CHECK(storage.open_handles.empty());
}
