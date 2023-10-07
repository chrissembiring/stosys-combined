/*
 * MIT License
Copyright (c) 2021 - current
Authors:  Animesh Trivedi
This code is part of the Storage System Course at VU Amsterdam
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#ifndef STOSYS_PROJECT_S2FILESYSTEM_H
#define STOSYS_PROJECT_S2FILESYSTEM_H

#include "rocksdb/env.h"
#include "rocksdb/io_status.h"
#include "rocksdb/file_system.h"
#include "rocksdb/status.h"

#include <zns_device.h>
#include <iostream>
#include <unordered_map>

namespace ROCKSDB_NAMESPACE {

    class S2FileSystem : public FileSystem {
    public:
        // No copying allowed
        S2FileSystem(std::string uri, bool debug);
        S2FileSystem(const S2FileSystem&) = delete;
        virtual ~S2FileSystem();

        IOStatus IsDirectory(const std::string &, const IOOptions &options, bool *is_dir, IODebugContext *) override;

        IOStatus
        NewSequentialFile(const std::string &fname, const FileOptions &file_opts,
                          std::unique_ptr<FSSequentialFile> *result,
                          __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        NewRandomAccessFile(const std::string &fname, const FileOptions &file_opts,
                            std::unique_ptr<FSRandomAccessFile> *result,
                            __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        NewWritableFile(const std::string &fname, const FileOptions &file_opts, std::unique_ptr<FSWritableFile> *result,
                        __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        ReopenWritableFile(const std::string &, const FileOptions &, std::unique_ptr<FSWritableFile> *,
                           IODebugContext *);

        IOStatus
        NewRandomRWFile(const std::string &, const FileOptions &, std::unique_ptr<FSRandomRWFile> *, IODebugContext *);

        IOStatus NewMemoryMappedFileBuffer(const std::string &, std::unique_ptr<MemoryMappedFileBuffer> *);

        IOStatus NewDirectory(const std::string &name, const IOOptions &io_opts, std::unique_ptr<FSDirectory> *result,
                              __attribute__ ((unused)) IODebugContext *dbg);

        const char *Name() const;

        IOStatus GetFreeSpace(const std::string &, const IOOptions &, uint64_t *, IODebugContext *);

        IOStatus Truncate(const std::string &, size_t, const IOOptions &, IODebugContext *);

        IOStatus CreateDir(const std::string &dirname, const IOOptions &options, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus CreateDirIfMissing(const std::string &dirname, const IOOptions &options, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        GetFileSize(const std::string &fname, const IOOptions &options, uint64_t *file_size, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus DeleteDir(const std::string &dirname, const IOOptions &options, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        GetFileModificationTime(const std::string &fname, const IOOptions &options, uint64_t *file_mtime,
                                __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        GetAbsolutePath(const std::string &db_path, const IOOptions &options, std::string *output_path,
                        __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus DeleteFile(const std::string& fname,
                            const IOOptions& options,
                            IODebugContext* dbg);

        IOStatus
        NewLogger(const std::string &fname, const IOOptions &io_opts, std::shared_ptr<Logger> *result,
                  __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus GetTestDirectory(const IOOptions &options, std::string *path, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus UnlockFile(FileLock *lock, const IOOptions &options, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus LockFile(const std::string &fname, const IOOptions &options, FileLock **lock, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus AreFilesSame(const std::string &, const std::string &, const IOOptions &, bool *, IODebugContext *);

        IOStatus NumFileLinks(const std::string &, const IOOptions &, uint64_t *, IODebugContext *);

        IOStatus LinkFile(const std::string &, const std::string &, const IOOptions &, IODebugContext *);

        IOStatus
        RenameFile(const std::string &src, const std::string &target, const IOOptions &options, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        GetChildrenFileAttributes(const std::string &dir, const IOOptions &options, std::vector<FileAttributes> *result,
                                  __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus
        GetChildren(const std::string &dir, const IOOptions &options, std::vector<std::string> *result,
                    __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus FileExists(const std::string &fname, const IOOptions &options, __attribute__ ((unused)) IODebugContext *dbg);

        IOStatus ReuseWritableFile(const std::string &fname, const std::string &old_fname, const FileOptions &file_opts,
                                   std::unique_ptr<FSWritableFile> *result, __attribute__ ((unused)) IODebugContext *dbg);

    private:
        struct user_zns_device *_zns_dev;
        std::string _uri;
        const std::string _fs_delimiter = "/";
    };

    class S2FileSystemDirectory : public FSDirectory {
        private:

        public:
            S2FileSystemDirectory();
            ~S2FileSystemDirectory();
    };

    class S2FileSystemWritableFile : public FSWritableFile {
        private:

        public:
            S2FileSystemWritableFile();
            ~S2FileSystemWritableFile();
            
            // Append data to the end of the file
            // Note: A WriteableFile object must support either Append or
            // PositionedAppend, so the users cannot mix the two.
            virtual IOStatus Append(const Slice& data, const IOOptions& options,
                                      IODebugContext* dbg) = 0;

            // Append data with verification information.
            // Note that this API change is experimental and it might be changed in
            // the future. Currently, RocksDB only generates crc32c based checksum for
            // the file writes when the checksum handoff option is set.
            // Expected behavior: if the handoff_checksum_type in FileOptions (currently,
            // ChecksumType::kCRC32C is set as default) is not supported by this
            // FSWritableFile, the information in DataVerificationInfo can be ignored
            // (i.e. does not perform checksum verification).
            virtual IOStatus Append(const Slice& data, const IOOptions& options,
                                    const DataVerificationInfo& /* verification_info */,
                                    IODebugContext* dbg) {
            return Append(data, options, dbg);
            }

            virtual IOStatus Close(const IOOptions& options, IODebugContext* dbg) = 0;
            virtual IOStatus Flush(const IOOptions& options, IODebugContext* dbg) = 0;
            virtual IOStatus Sync(const IOOptions& options,
                                    IODebugContext* dbg) = 0;  // sync data

    };  
}

#endif //STOSYS_PROJECT_S2FILESYSTEM_H
