/*
	This file is part of the Util library.
	Copyright (C) 2007-2012 Benjamin Eikel <benjamin@eikel.org>
	Copyright (C) 2007-2012 Claudius Jähn <claudius@uni-paderborn.de>
	Copyright (C) 2007-2012 Ralf Petring <ralf@petring.net>
	
	This library is subject to the terms of the Mozilla Public License, v. 2.0.
	You should have received a copy of the MPL along with this library; see the 
	file LICENSE. If not, you can obtain one at http://mozilla.org/MPL/2.0/.
*/
#include "StreamerSDLImage.h"
#include "Serialization.h"
#include "../Factory/Factory.h"
#include "../Graphics/Bitmap.h"
#include "../Graphics/BitmapUtils.h"
#include "../Macros.h"
#include <cstddef>

#if defined(UTIL_HAVE_LIB_SDL2) and defined(UTIL_HAVE_LIB_SDL2_IMAGE)
#include <SDL.h>
#include <SDL_image.h>
#endif /* defined(UTIL_HAVE_LIB_SDL2) and defined(UTIL_HAVE_LIB_SDL2_IMAGE) */

namespace Util {

#if defined(UTIL_HAVE_LIB_SDL2) and defined(UTIL_HAVE_LIB_SDL2_IMAGE)

Reference<Bitmap> StreamerSDLImage::loadBitmap(std::istream & input) {
	input.seekg(0, std::ios::end);
	std::streampos size = input.tellg();
	input.seekg(0, std::ios::beg);

	std::vector<uint8_t> data(size);
	input.read(reinterpret_cast<char *>(data.data()), size);

	SDL_Surface * surface = IMG_Load_RW(SDL_RWFromConstMem(data.data(), data.size()), true);
	if (surface == nullptr) {
		WARN(std::string("Could not create image. ") + IMG_GetError());
		return nullptr;
	}
	auto bitmap = BitmapUtils::createBitmapFromSDLSurface(surface);
	SDL_FreeSurface(surface);
	return std::move(bitmap);
}

#endif /* defined(UTIL_HAVE_LIB_SDL2) and defined(UTIL_HAVE_LIB_SDL2_IMAGE) */

bool StreamerSDLImage::init() {
#if defined(UTIL_HAVE_LIB_SDL2) and defined(UTIL_HAVE_LIB_SDL2_IMAGE)
	static const std::string fileExtensions[18] = { "bmp", "gif", "iff", "jpeg", "jpg", "lbm", "pbm", "pcx", "pgm", "png", "pnm", "ppm", "tga", "tif", "tiff", "xcf", "xpm", "xv" };
	for(auto & fileExtension : fileExtensions) {
		Serialization::registerBitmapLoader(fileExtension, ObjectCreator<StreamerSDLImage>());
	}
#endif /* defined(UTIL_HAVE_LIB_SDL2) and defined(UTIL_HAVE_LIB_SDL2_IMAGE) */
	return true;
}

}
