/*
	This file is part of the Util library.
	Copyright (C) 2012 Benjamin Eikel <benjamin@eikel.org>
	
	This library is subject to the terms of the Mozilla Public License, v. 2.0.
	You should have received a copy of the MPL along with this library; see the 
	file LICENSE. If not, you can obtain one at http://mozilla.org/MPL/2.0/.
*/
#ifdef UTIL_HAVE_LIB_ARCHIVE

#ifndef UTIL_IO_ARCHIVEPROVIDER_H
#define UTIL_IO_ARCHIVEPROVIDER_H

#include "AbstractFSProvider.h"
#include "FileName.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Util {
namespace Concurrency {
class Mutex;
}
class Archive;

/**
 * File system provider for access to archive files using the
 * "[type]://path/to/archive$file_in_archive" URL scheme.
 *
 * @author Benjamin Eikel
 * @date 2012-10-11
 */
class ArchiveProvider : public AbstractFSProvider {
	public:
		static bool init();

		ArchiveProvider();
		virtual ~ArchiveProvider();

		status_t readFile(const FileName & url, std::vector<uint8_t> & data) override;
		status_t writeFile(const FileName & url, const std::vector<uint8_t> & data, bool overwrite) override;

		status_t dir(const FileName & url, std::list<FileName> & result, uint8_t flags) override;
		bool isFile(const FileName & url) override;
		bool isDir(const FileName & url) override;
		size_t fileSize(const FileName & url) override;

		status_t makeDir(const FileName & url) override;
		status_t makeDirRecursive(const FileName & url) override;
		status_t remove(const FileName & url) override;

		void flush() override;

	private:
		ArchiveProvider(const ArchiveProvider &) = delete;
		ArchiveProvider(ArchiveProvider &&) = delete;
		ArchiveProvider & operator=(const ArchiveProvider &) = delete;
		ArchiveProvider & operator=(ArchiveProvider &&) = delete;

		std::unique_ptr<Concurrency::Mutex> archiveMutex;
		std::unordered_map<std::string, std::unique_ptr<Archive>> openArchives;

		Archive * getHandle(const std::string & archiveFileName);
		void decomposeURL(const FileName & url, std::string & archiveFileName, FileName & localPath);
};
}
#endif	/* UTIL_IO_ARCHIVEPROVIDER_H */

#endif /* UTIL_HAVE_LIB_ARCHIVE */
