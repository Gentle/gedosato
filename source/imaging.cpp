#include "imaging.h"

#include "console.h"
#include "utils/d3d9_utils.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"
#undef STB_IMAGE_WRITE_IMPLEMENTATION

#include <boost/algorithm/string/replace.hpp>

void ImageWriter::writeSurface(const string& fn, IDirect3DSurface9* surf, bool discardAlpha) {
	//{
	//	D3DSURFACE_DESC desc, tdesc;
	//	surf->GetDesc(&desc);
	//	tempSurf->GetDesc(&tdesc);
	//	SDLOG(-1, "Screenshot surface: %s\n - tmp surface: %s\n", D3DSurfaceDescToString(desc), D3DSurfaceDescToString(tdesc));
	//}

	// Octagon screenshot mode
	if(Settings::get().getMaxScreenshotParallelism() == -1) {
		string fnc = fn;
		boost::algorithm::replace_first(fnc, ".png", ".bmp");
		if(D3DXSaveSurfaceToFile(fnc.c_str(), D3DXIFF_BMP, surf, NULL, NULL) != D3D_OK) {
			Console::get().add("Failed saving screenshot! (D3DX)");
		}
		return;
	}

	// wait for processing if maximum degree of parallelism reached
	if(static_cast<int>(futures.size()) > Settings::get().getMaxScreenshotParallelism()) {
		auto& f = futures.front();
		if(f.valid()) f.wait();
		futures.pop();
	}

	// get image data into buffer
	D3DSURFACE_DESC desc;
	surf->GetDesc(&desc);
	RECT r;
	r.left = 0;
	r.top = 0;
	r.right = desc.Width;
	r.bottom = desc.Height;
	HRESULT hr = device9->StretchRect(surf, &r, tempSurf, &r, D3DTEXF_NONE);
	if(hr != D3D_OK) {
		Console::get().add("Failed taking screenshot! (StretchRect)");
		return;
	}
	D3DLOCKED_RECT lockedR;
	if(tempSurf->LockRect(&lockedR, &r, D3DLOCK_READONLY) == D3D_OK) {
		BYTE* buffer = new BYTE[lockedR.Pitch*desc.Height];
		memcpy(buffer, lockedR.pBits, lockedR.Pitch*desc.Height);
		INT pitch = lockedR.Pitch;
		tempSurf->UnlockRect();

		// lambda performing image encoding and writing
		auto writer = [this, buffer, desc, pitch, fn, discardAlpha] {
			for(unsigned i = 0; i < pitch*desc.Height; i += 4) {
				// A8R8G8B8 --> (A)BGR
				BYTE tmp = buffer[i + 0];
				buffer[i + 0] = buffer[i + 2];
				buffer[i + 1] = buffer[i + 1];
				buffer[i + 2] = tmp;
				if(discardAlpha) buffer[i + 3] = 255;
			}
			if(stbi_write_png(fn.c_str(), desc.Width, desc.Height, 4, buffer, pitch) == 0) {
				Console::get().add("Failed taking screenshot! (STBI)");
			}
			delete[] buffer;
		};

		if(Settings::get().getMaxScreenshotParallelism() > 0) {
			// perform image encoding and writing in parallel
			futures.push(std::async(writer));
		}
		else {
			writer();
		}
	}
	else {
		Console::get().add("Failed taking screenshot! (LockRect)");
	}
}
