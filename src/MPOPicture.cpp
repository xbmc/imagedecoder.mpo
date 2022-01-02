/*
 *  Copyright (C) 2005-2021 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "MPOPicture.h"

#include "../lib/TinyEXIF/TinyEXIF.h"

#include <iostream>
#include <kodi/Filesystem.h>

MPOPicture::MPOPicture(KODI_HANDLE instance, const std::string& version)
  : CInstanceImageDecoder(instance, version)
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
  mpo_mem_src(&mpoinfo, buffer.data(), buffer.size());
  bool ret = mpo_read_header(&mpoinfo);
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
    tm.tm_isdst = -1; // Assume local daylight setting per date/time
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
  // make a copy of data as we need it at decode time.
  m_data.resize(bufSize);
  std::copy(buffer, buffer + bufSize, m_data.begin());
  mpo_create_decompress(&m_mpoinfo);
  mpo_mem_src(&m_mpoinfo, m_data.data(), m_data.size());
  if (!mpo_read_header(&m_mpoinfo))
  {
    mpo_destroy_decompress(&m_mpoinfo);
    return false;
  }
  m_allocated = true;
  m_images = mpo_get_number_images(&m_mpoinfo);
  m_width = width = m_mpoinfo.cinfo.cinfo.image_width * m_images;
  m_height = height = m_mpoinfo.cinfo.cinfo.image_height;

  return true;
}

bool MPOPicture::Decode(uint8_t* pixels,
                        unsigned int width,
                        unsigned int height,
                        unsigned int pitch,
                        ADDON_IMG_FMT format)
{
  size_t image = 0;
  while (image < m_images)
  {
    mpo_start_decompress(&m_mpoinfo);
    JSAMPARRAY buffer;
    int row_stride = m_mpoinfo.cinfo.cinfo.output_width * m_mpoinfo.cinfo.cinfo.output_components;
    size_t lines = 0;
    while (lines < m_height)
    {
      buffer = (*m_mpoinfo.cinfo.cinfo.mem->alloc_sarray)((j_common_ptr)&m_mpoinfo.cinfo,
                                                          JPOOL_IMAGE, row_stride, m_height);
      size_t nl = mpo_read_scanlines(&m_mpoinfo, buffer, m_height - lines);
      for (size_t line = 0; line < nl; ++line)
      {
        unsigned char* dst = pixels + (line + lines) * pitch + image * m_width / 2 * 4;
        for (size_t i = 0; i < row_stride; i += 3)
        {
          *dst++ = buffer[line][i + 2];
          *dst++ = buffer[line][i + 1];
          *dst++ = buffer[line][i];
          if (format == ADDON_IMG_FMT_A8R8G8B8)
            *dst++ = 0xff;
        }
      }
      lines += nl;
    }
    mpo_finish_decompress(&m_mpoinfo);
    ++image;
  }

  return true;
}

class ATTR_DLL_LOCAL CMyAddon : public kodi::addon::CAddonBase
{
public:
  CMyAddon() = default;
  ADDON_STATUS CreateInstance(int instanceType,
                              const std::string& instanceID,
                              KODI_HANDLE instance,
                              const std::string& version,
                              KODI_HANDLE& addonInstance) override
  {
    addonInstance = new MPOPicture(instance, version);
    return ADDON_STATUS_OK;
  }
};

ADDONCREATOR(CMyAddon)
