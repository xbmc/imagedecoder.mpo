/*
 *  Copyright (C) 2005-2021 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "MPOPicture.h"

#include "../lib/TinyEXIF/TinyEXIF.h"

#include <csetjmp>
#include <cstdlib>
#include <iostream>
#include <kodi/Filesystem.h>

namespace
{

thread_local std::jmp_buf s_jpegEscape;

void MpoFatalError(j_common_ptr cinfo)
{
  char message[JMSG_LENGTH_MAX] = {};
  (*cinfo->err->format_message)(cinfo, message);
  kodi::Log(ADDON_LOG_ERROR, "libjpeg: %s", message);
  std::longjmp(s_jpegEscape, 1);
}

} // namespace

MPOPicture::MPOPicture(const kodi::addon::IInstanceInfo& instance)
  : CInstanceImageDecoder(instance)
{
}

MPOPicture::~MPOPicture()
{
  if (m_allocated)
    mpo_destroy_decompress(&m_mpoinfo);
  m_allocated = false;
}

bool MPOPicture::SupportsFile(const std::string& file)
{
  kodi::vfs::CFile fileData;
  if (!fileData.OpenFile(file))
    return false;

  std::vector<uint8_t> buffer(fileData.GetLength());
  fileData.Read(buffer.data(), buffer.size());

  mpo_decompress_struct mpoinfo;
  mpo_create_decompress(&mpoinfo);
  mpo_decompress_error_exit(&mpoinfo, MpoFatalError);
  if (setjmp(s_jpegEscape))
  {
    mpo_destroy_decompress(&mpoinfo);
    return false;
  }

  mpo_mem_src(&mpoinfo, buffer.data(), buffer.size());
  const bool ret = mpo_read_header(&mpoinfo);
  mpo_destroy_decompress(&mpoinfo);
  return ret;
}

bool MPOPicture::ReadTag(const std::string& file, kodi::addon::ImageDecoderInfoTag& tag)
{
  kodi::vfs::CFile fileData;
  if (!fileData.OpenFile(file))
    return false;

  const size_t length = fileData.GetLength();

  std::vector<uint8_t> buffer(length);
  fileData.Read(buffer.data(), buffer.size());

  TinyEXIF::EXIFInfo imageEXIF;
  if (imageEXIF.parseFrom(buffer.data(), buffer.size()) != 0)
    return false;

  tag.SetWidth(imageEXIF.ImageWidth);
  tag.SetHeight(imageEXIF.ImageHeight);

  switch (imageEXIF.Orientation)
  {
    case 3:
      tag.SetOrientation(ADDON_IMG_ORIENTATION_ROTATE_180_CCW);
      break;
    case 5:
      tag.SetOrientation(ADDON_IMG_ORIENTATION_ROTATE_90_CCW);
      break;
    case 6:
      tag.SetOrientation(ADDON_IMG_ORIENTATION_ROTATE_270_CCW);
      break;
    default:
      tag.SetOrientation(ADDON_IMG_ORIENTATION_NONE);
      break;
  }

  std::string dateTime;
  if (!imageEXIF.DateTimeOriginal.empty())
    dateTime = imageEXIF.DateTimeOriginal;
  else if (!imageEXIF.DateTime.empty())
    dateTime = imageEXIF.DateTime;
  else if (!imageEXIF.DateTimeDigitized.empty())
    dateTime = imageEXIF.DateTimeDigitized;
  if (dateTime.size() == 19)
  {
    struct tm tm = {0};
    tm.tm_year = atoi(dateTime.substr(0, 4).c_str()) - 1900;
    tm.tm_mon = atoi(dateTime.substr(5, 2).c_str()) - 1;
    tm.tm_mday = atoi(dateTime.substr(8, 2).c_str());
    tm.tm_hour = atoi(dateTime.substr(11, 2).c_str());
    tm.tm_min = atoi(dateTime.substr(14, 2).c_str());
    tm.tm_sec = atoi(dateTime.substr(17, 2).c_str());
    tm.tm_isdst = -1;
    tag.SetTimeCreated(mktime(&tm));
  }
  tag.SetDistance(imageEXIF.SubjectDistance);
  tag.SetISOSpeed(imageEXIF.ISOSpeedRatings);
  tag.SetFocalLength(imageEXIF.FocalLength);
  tag.SetFocalLengthIn35mmFormat(imageEXIF.LensInfo.FocalLengthIn35mm);
  tag.SetCameraManufacturer(imageEXIF.Make);
  tag.SetCameraModel(imageEXIF.Model);
  tag.SetExposureBias(imageEXIF.ExposureBiasValue);
  tag.SetExposureTime(imageEXIF.ExposureTime);
  tag.SetExposureProgram(static_cast<ADDON_IMG_EXPOSURE_PROGRAM>(imageEXIF.ExposureProgram));
  tag.SetMeteringMode(static_cast<ADDON_IMG_METERING_MODE>(imageEXIF.MeteringMode));
  tag.SetApertureFNumber(imageEXIF.FNumber);
  tag.SetFlashUsed(static_cast<ADDON_IMG_FLASH_TYPE>(imageEXIF.Flash));
  tag.SetLightSource(static_cast<ADDON_IMG_LIGHT_SOURCE>(imageEXIF.LightSource));
  tag.SetDescription(imageEXIF.ImageDescription);
  tag.SetDigitalZoomRatio(imageEXIF.LensInfo.DigitalZoomRatio);

  if (imageEXIF.GeoLocation.hasLatLon() && imageEXIF.GeoLocation.hasAltitude() &&
      isalpha(imageEXIF.GeoLocation.LatComponents.direction))
  {
    float lat[] = {static_cast<float>(imageEXIF.GeoLocation.LatComponents.degrees),
                   static_cast<float>(imageEXIF.GeoLocation.LatComponents.minutes),
                   static_cast<float>(imageEXIF.GeoLocation.LatComponents.seconds)};

    float lon[] = {static_cast<float>(imageEXIF.GeoLocation.LonComponents.degrees),
                   static_cast<float>(imageEXIF.GeoLocation.LonComponents.minutes),
                   static_cast<float>(imageEXIF.GeoLocation.LonComponents.seconds)};
    tag.SetGPSInfo(true, imageEXIF.GeoLocation.LatComponents.direction, lat,
                   imageEXIF.GeoLocation.LonComponents.direction, lon,
                   imageEXIF.GeoLocation.AltitudeRef, imageEXIF.GeoLocation.Altitude);
  }

  return true;
}

bool MPOPicture::LoadImageFromMemory(const std::string& mimetype,
                                     const uint8_t* buffer,
                                     size_t bufSize,
                                     unsigned int& width,
                                     unsigned int& height)
{
  m_data.resize(bufSize);
  std::copy(buffer, buffer + bufSize, m_data.begin());
  mpo_create_decompress(&m_mpoinfo);
  mpo_decompress_error_exit(&m_mpoinfo, MpoFatalError);
  if (setjmp(s_jpegEscape))
  {
    mpo_destroy_decompress(&m_mpoinfo);
    return false;
  }

  mpo_mem_src(&m_mpoinfo, m_data.data(), m_data.size());
  if (!mpo_read_header(&m_mpoinfo))
  {
    mpo_destroy_decompress(&m_mpoinfo);
    return false;
  }
  m_allocated = true;
  m_images = mpo_get_number_images(&m_mpoinfo);
  
  // Always advertise fixed 1080p canvas (1920x1080) for Half-SBS output
  m_width = width = 1920;
  m_height = height = 1080;

  return true;
}

bool MPOPicture::Decode(uint8_t* pixels,
                        size_t pixelBufferSize,
                        unsigned int width,
                        unsigned int height,
                        unsigned int pitch,
                        ADDON_IMG_FMT format)
{
  if (format != ADDON_IMG_FMT_A8R8G8B8)
  {
    kodi::Log(ADDON_LOG_ERROR, "%s: Unsupported target format (%d)", __func__,
              static_cast<int>(format));
    return false;
  }

  const int targetH = 1080;
  const int eyeTargetW = 960;

  // Fix #2: Fill background with OPAQUE black (Alpha = 0xFF)
  uint32_t* bgPixels = reinterpret_cast<uint32_t*>(pixels);
  const size_t totalPixels = pixelBufferSize / sizeof(uint32_t);
  for (size_t i = 0; i < totalPixels; ++i)
  {
    bgPixels[i] = 0xFF000000;
  }

  // Fix #1: Force RGB output mode to avoid CMYK 4-component overflow
  m_mpoinfo.cinfo.cinfo.out_color_space = JCS_RGB;

  size_t image = 0;
  while (image < m_images && image < 2)
  {
    if (setjmp(s_jpegEscape))
      return false;

    mpo_start_decompress(&m_mpoinfo);
    
    const int srcW = m_mpoinfo.cinfo.cinfo.output_width;
    const int srcH = m_mpoinfo.cinfo.cinfo.output_height;

    if (srcW <= 0 || srcH <= 0)
    {
      mpo_finish_decompress(&m_mpoinfo);
      return false;
    }

    // Fix #3: Use malloc/free to remain safe with longjmp (no vector destructor skip)
    uint8_t* eyeBuffer = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(srcW) * srcH * 3));
    if (!eyeBuffer)
    {
      mpo_finish_decompress(&m_mpoinfo);
      return false;
    }

    int lines = 0;
    while (lines < srcH)
    {
      JSAMPROW row_pointer = eyeBuffer + (lines * srcW * 3);
      if (mpo_read_scanlines(&m_mpoinfo, &row_pointer, 1) != 1)
      {
        std::free(eyeBuffer);
        mpo_finish_decompress(&m_mpoinfo);
        return false;
      }
      lines++;
    }

    mpo_finish_decompress(&m_mpoinfo);

    const float xRatio = static_cast<float>(srcW) / static_cast<float>(eyeTargetW);
    const float yRatio = static_cast<float>(srcH) / static_cast<float>(targetH);
    const int xOffset = (image == 0) ? 0 : eyeTargetW;

    for (int dy = 0; dy < targetH; ++dy)
    {
      float srcY = dy * yRatio;
      int y0 = static_cast<int>(srcY);
      int y1 = (y0 + 1 < srcH) ? y0 + 1 : y0;
      float yDiff = srcY - y0;

      uint8_t* dstRow = pixels + dy * pitch + xOffset * 4;

      for (int dx = 0; dx < eyeTargetW; ++dx)
      {
        float srcX = dx * xRatio;
        int x0 = static_cast<int>(srcX);
        int x1 = (x0 + 1 < srcW) ? x0 + 1 : x0;
        float xDiff = srcX - x0;

        const uint8_t* p00 = eyeBuffer + (y0 * srcW + x0) * 3;
        const uint8_t* p10 = eyeBuffer + (
