#define WIN32_LEAN_AND_MEAN
#include <glad/glad.h>
#include <wincodec.h>
#include <windows.h>
#include "app.h"

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

/* ----------------------------------------------------------------- load -- */

#define LOAD_ICON(path, tex_field, img_field, ready_field)          \
    if (load_texture_rgba(path, &S->tex_field)) {                   \
        S->img_field  = nk_image_id((int)S->tex_field);             \
        S->ready_field = 1;                                         \
    }

void ui_icons_load(struct AppState *S) {
    S->warn_ready = S->ok_ready = S->danger_ready =
    S->github_ready = S->win_ready = 0;

    LOAD_ICON("resources\\icons\\warn.png",    tex_warn,   icon_warn,   warn_ready)
    LOAD_ICON("resources\\icons\\ok.png",      tex_ok,     icon_ok,     ok_ready)
    LOAD_ICON("resources\\icons\\danger.png",  tex_danger, icon_danger, danger_ready)
    LOAD_ICON("resources\\icons\\github.png",  tex_github, icon_github, github_ready)
    LOAD_ICON("resources\\icons\\windows.png", tex_win,    icon_win,    win_ready)
}

/* ----------------------------------------------------------------- free -- */

void ui_icons_free(struct AppState *S) {
    unsigned int *textures[] = {
        &S->tex_warn, &S->tex_ok, &S->tex_danger,
        &S->tex_github, &S->tex_win,
    };
    for (int i = 0; i < 5; i++) {
        if (*textures[i]) {
            GLuint t = (GLuint)*textures[i];
            glDeleteTextures(1, &t);
            *textures[i] = 0;
        }
    }
}
