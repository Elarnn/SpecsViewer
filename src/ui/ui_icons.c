#define WIN32_LEAN_AND_MEAN
#include <glad/glad.h>
#include <wincodec.h>
#include <windows.h>


#pragma comment(lib, "windowscodecs.lib")

int load_texture_rgba(const char *path, unsigned int *out_tex) {
  HRESULT hr;
  IWICImagingFactory *factory = NULL;
  IWICBitmapDecoder *decoder = NULL;
  IWICBitmapFrameDecode *frame = NULL;
  IWICFormatConverter *converter = NULL;

  wchar_t wpath[MAX_PATH];
  MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);

  hr = CoInitialize(NULL);
  if (FAILED(hr))
    return 0;

  hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                        &IID_IWICImagingFactory, (LPVOID *)&factory);
  if (FAILED(hr))
    goto cleanup;

  hr = factory->lpVtbl->CreateDecoderFromFilename(
      factory, wpath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
      &decoder);
  if (FAILED(hr))
    goto cleanup;

  hr = decoder->lpVtbl->GetFrame(decoder, 0, &frame);
  if (FAILED(hr))
    goto cleanup;

  hr = factory->lpVtbl->CreateFormatConverter(factory, &converter);
  if (FAILED(hr))
    goto cleanup;

  hr = converter->lpVtbl->Initialize(
      converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppRGBA,
      WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr))
    goto cleanup;

  UINT w = 0, h = 0;
  converter->lpVtbl->GetSize(converter, &w, &h);

  unsigned char *pixels = (unsigned char *)malloc(w * h * 4);
  if (!pixels)
    goto cleanup;

  hr = converter->lpVtbl->CopyPixels(converter, NULL, w * 4, w * h * 4, pixels);
  if (FAILED(hr)) {
    free(pixels);
    goto cleanup;
  }

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);

  free(pixels);

  *out_tex = (unsigned int)tex;

cleanup:
  if (converter)
    converter->lpVtbl->Release(converter);
  if (frame)
    frame->lpVtbl->Release(frame);
  if (decoder)
    decoder->lpVtbl->Release(decoder);
  if (factory)
    factory->lpVtbl->Release(factory);

  CoUninitialize();
  return SUCCEEDED(hr);
}
