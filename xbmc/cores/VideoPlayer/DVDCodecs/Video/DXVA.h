/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */
#pragma once

#include <list>
#include <vector>
#include "DVDResource.h"
#include "guilib/D3DResource.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/d3d11va.h"
#include "threads/Event.h"
#include "cores/VideoPlayer/Process/VideoBuffer.h"

class CProcessInfo;

namespace DXVA {

#define CHECK(a) \
do { \
  HRESULT res = a; \
  if(FAILED(res)) \
  { \
    CLog::Log(LOGERROR, "DXVA - failed executing "#a" at line %d with error %x", __LINE__, res); \
    return false; \
  } \
} while(0);

class CSurfaceContext : public IDVDResourceCounted<CSurfaceContext>
{
public:
  CSurfaceContext();
  virtual ~CSurfaceContext();

  void AddSurface(ID3D11View* view);
  void ClearReference(ID3D11View* view);
  bool MarkRender(ID3D11View* view);
  void ClearRender(ID3D11View* view);
  bool IsValid(ID3D11View* view);
  ID3D11View* GetFree(ID3D11View* view);
  ID3D11View* GetAtIndex(unsigned int idx);
  int Size();
  bool HasFree();
  bool HasRefs();

protected:
  void Reset();
  std::map<ID3D11View*, int> m_state;
  std::list<ID3D11View*> m_freeViews;
  CCriticalSection m_section;
};

class CRenderPicture
  : public IDVDResourceCounted<CRenderPicture>
{
public:
  CRenderPicture(CSurfaceContext *context);
  ~CRenderPicture();
  ID3D11View* view;
  DXGI_FORMAT format;

protected:
  CSurfaceContext *surface_context;
};

class CDXVAVideoBuffer : public CVideoBuffer
{
public:
  CDXVAVideoBuffer(int id) : CVideoBuffer(id), picture(nullptr)
  {
    m_pixFormat = AV_PIX_FMT_D3D11VA_VLD;
  }
  CRenderPicture* picture;
};

class CDXVABufferPool : public IVideoBufferPool
{
public:
  CDXVABufferPool();
  virtual ~CDXVABufferPool();

  CVideoBuffer* Get() override;
  void Return(int id) override;
  CDXVAVideoBuffer* GetDX() { return reinterpret_cast<CDXVAVideoBuffer*>(Get()); }

private:
  CCriticalSection m_critSection;
  std::vector<CDXVAVideoBuffer*> m_all;
  std::deque<int> m_used;
  std::deque<int> m_free;
};

class CDecoder;
class CDXVAContext
{
public:
  static bool EnsureContext(CDXVAContext **ctx, CDecoder *decoder);
  bool GetInputAndTarget(int codec, bool bHighBitdepth, GUID &inGuid, DXGI_FORMAT &outFormat);
  bool GetConfig(const D3D11_VIDEO_DECODER_DESC *format, D3D11_VIDEO_DECODER_CONFIG &config);
  bool CreateSurfaces(D3D11_VIDEO_DECODER_DESC format, unsigned int count, unsigned int alignment, ID3D11VideoDecoderOutputView **surfaces);
  bool CreateDecoder(D3D11_VIDEO_DECODER_DESC *format, const D3D11_VIDEO_DECODER_CONFIG *config, ID3D11VideoDecoder **decoder, ID3D11VideoContext **context);
  void Release(CDecoder *decoder);
  ID3D11VideoContext* GetVideoContext() const { return m_vcontext; }

private:
  CDXVAContext();
  void Close();
  bool CreateContext();
  void DestroyContext();
  void QueryCaps();
  bool IsValidDecoder(CDecoder *decoder);

  static CDXVAContext *m_context;
  static CCriticalSection m_section;

  ID3D11VideoContext* m_vcontext;
  ID3D11VideoDevice* m_service;
  int m_refCount;
  UINT m_input_count;
  GUID *m_input_list;
  std::vector<CDecoder*> m_decoders;
  bool m_atiWorkaround;
};

class CDecoder
  : public IHardwareDecoder
  , public ID3DResource
{
public:
  CDecoder(CProcessInfo& processInfo);
 ~CDecoder();

  // IHardwareDecoder overrides
  bool Open(AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat) override;
  CDVDVideoCodec::VCReturn Decode(AVCodecContext* avctx, AVFrame* frame) override;
  bool GetPicture(AVCodecContext* avctx, VideoPicture* picture) override;
  CDVDVideoCodec::VCReturn Check(AVCodecContext* avctx) override;
  const std::string Name() override { return "d3d11va"; }
  unsigned GetAllowedReferences() override;

  // IDVDResourceCounted overrides
  long Release() override;

  bool OpenDecoder();
  int GetBuffer(AVCodecContext *avctx, AVFrame *pic);
  void ReleaseBuffer(uint8_t *data);
  void Close();
  void CloseDXVADecoder();

  //static members
  static bool Supports(enum AVPixelFormat fmt);
  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);
  static void FFReleaseBuffer(void *opaque, uint8_t *data);

protected:
  enum EDeviceState
  { DXVA_OPEN
  , DXVA_RESET
  , DXVA_LOST
  } m_state;

  // ID3DResource overrides
  void OnCreateDevice() override  {}
  void OnDestroyDevice(bool fatal) override { CSingleLock lock(m_section); m_state = DXVA_LOST;  m_event.Reset(); }
  void OnLostDevice() override    { CSingleLock lock(m_section); m_state = DXVA_LOST;  m_event.Reset(); }
  void OnResetDevice() override   { CSingleLock lock(m_section); m_state = DXVA_RESET; m_event.Set();   }

  int m_refs;
  HANDLE m_device;
  ID3D11VideoDecoder *m_decoder;
  ID3D11VideoContext *m_vcontext;
  D3D11_VIDEO_DECODER_DESC m_format;
  CDXVAVideoBuffer *m_presentPicture;
  struct AVD3D11VAContext *m_context;
  CSurfaceContext *m_surface_context;
  CDXVAContext *m_dxva_context;
  AVCodecContext *m_avctx;
  unsigned int m_shared;
  unsigned int m_surface_alignment;
  CCriticalSection m_section;
  CEvent m_event;
  CProcessInfo& m_processInfo;
  std::shared_ptr<CDXVABufferPool> m_bufferPool;
};

};
