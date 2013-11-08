/*
	This file is part of the Util library.
	Copyright (C) 2007-2012 Benjamin Eikel <benjamin@eikel.org>
	Copyright (C) 2007-2012 Claudius Jähn <claudius@uni-paderborn.de>
	Copyright (C) 2007-2012 Ralf Petring <ralf@petring.net>
	
	This library is subject to the terms of the Mozilla Public License, v. 2.0.
	You should have received a copy of the MPL along with this library; see the 
	file LICENSE. If not, you can obtain one at http://mozilla.org/MPL/2.0/.
*/
#ifndef UTIL_STREAMERSDLIMAGE_H_
#define UTIL_STREAMERSDLIMAGE_H_

#include "AbstractBitmapStreamer.h"
#include "../References.h"
#include <istream>
#include <cstdint>
#include <string>

namespace Util {
class Bitmap;

class StreamerSDLImage : public AbstractBitmapStreamer {
	public:
		StreamerSDLImage() :
			AbstractBitmapStreamer() {
		}
		virtual ~StreamerSDLImage() {
		}

		Reference<Bitmap> loadBitmap(std::istream & input) override;

		static bool init();
};
}

#endif /* UTIL_STREAMERSDLIMAGE_H_ */
