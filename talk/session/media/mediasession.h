/*
 * libjingle
 * Copyright 2004 Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Types and classes used in media session descriptions.

#ifndef TALK_SESSION_MEDIA_MEDIASESSION_H_
#define TALK_SESSION_MEDIA_MEDIASESSION_H_

#include <algorithm>
#include <string>
#include <vector>

#include "talk/media/base/codec.h"
#include "talk/media/base/constants.h"
#include "talk/media/base/cryptoparams.h"
#include "talk/media/base/mediachannel.h"
#include "talk/media/base/mediaengine.h"  // For DataChannelType
#include "talk/media/base/streamparams.h"
#include "webrtc/p2p/base/sessiondescription.h"
#include "webrtc/p2p/base/transport.h"
#include "webrtc/p2p/base/transportdescriptionfactory.h"
#include "webrtc/base/scoped_ptr.h"

namespace cricket {

class ChannelManager;
typedef std::vector<AudioCodec> AudioCodecs;
typedef std::vector<VideoCodec> VideoCodecs;
typedef std::vector<DataCodec> DataCodecs;
typedef std::vector<CryptoParams> CryptoParamsVec;
typedef std::vector<RtpHeaderExtension> RtpHeaderExtensions;

enum MediaType {
  MEDIA_TYPE_AUDIO,
  MEDIA_TYPE_VIDEO,
  MEDIA_TYPE_DATA
};

std::string MediaTypeToString(MediaType type);

enum MediaContentDirection {
  MD_INACTIVE,
  MD_SENDONLY,
  MD_RECVONLY,
  MD_SENDRECV
};

enum CryptoType {
  CT_NONE,
  CT_SDES,
  CT_DTLS
};

// RTC4585 RTP/AVPF
extern const char kMediaProtocolAvpf[];
// RFC5124 RTP/SAVPF
extern const char kMediaProtocolSavpf[];

extern const char kMediaProtocolDtlsSavpf[];

extern const char kMediaProtocolRtpPrefix[];

extern const char kMediaProtocolSctp[];
extern const char kMediaProtocolDtlsSctp[];
extern const char kMediaProtocolUdpDtlsSctp[];
extern const char kMediaProtocolTcpDtlsSctp[];

// Options to control how session descriptions are generated.
const int kAutoBandwidth = -1;
const int kBufferedModeDisabled = 0;

struct MediaSessionOptions {
  MediaSessionOptions() :
      recv_audio(true),
      recv_video(false),
      data_channel_type(DCT_NONE),
      is_muc(false),
      vad_enabled(true),  // When disabled, removes all CN codecs from SDP.
      rtcp_mux_enabled(true),
      bundle_enabled(false),
      video_bandwidth(kAutoBandwidth),
      data_bandwidth(kDataMaxBandwidth) {
  }

  bool has_audio() const {
    return recv_audio || HasSendMediaStream(MEDIA_TYPE_AUDIO);
  }
  bool has_video() const {
    return recv_video || HasSendMediaStream(MEDIA_TYPE_VIDEO);
  }
  bool has_data() const { return data_channel_type != DCT_NONE; }

  // Add a stream with MediaType type and id.
  // All streams with the same sync_label will get the same CNAME.
  // All ids must be unique.
  void AddSendStream(MediaType type,
                 const std::string& id,
                 const std::string& sync_label);
  void AddSendVideoStream(const std::string& id,
                      const std::string& sync_label,
                      int num_sim_layers);
  void RemoveSendStream(MediaType type, const std::string& id);


  // Helper function.
  void AddSendStreamInternal(MediaType type,
                         const std::string& id,
                         const std::string& sync_label,
                         int num_sim_layers);

  bool HasSendMediaStream(MediaType type) const;

  bool recv_audio;
  bool recv_video;
  DataChannelType data_channel_type;
  bool is_muc;
  bool vad_enabled;
  bool rtcp_mux_enabled;
  bool bundle_enabled;
  // bps. -1 == auto.
  int video_bandwidth;
  int data_bandwidth;
  TransportOptions transport_options;

  struct Stream {
    Stream(MediaType type,
           const std::string& id,
           const std::string& sync_label,
           int num_sim_layers)
        : type(type), id(id), sync_label(sync_label),
          num_sim_layers(num_sim_layers) {
    }
    MediaType type;
    std::string id;
    std::string sync_label;
    int num_sim_layers;
  };

  typedef std::vector<Stream> Streams;
  Streams streams;
};

// "content" (as used in XEP-0166) descriptions for voice and video.
class MediaContentDescription : public ContentDescription {
 public:
  MediaContentDescription()
      : rtcp_mux_(false),
        bandwidth_(kAutoBandwidth),
        crypto_required_(CT_NONE),
        rtp_header_extensions_set_(false),
        multistream_(false),
        conference_mode_(false),
        partial_(false),
        buffered_mode_latency_(kBufferedModeDisabled),
        direction_(MD_SENDRECV) {
  }

  virtual MediaType type() const = 0;
  virtual bool has_codecs() const = 0;

  // |protocol| is the expected media transport protocol, such as RTP/AVPF,
  // RTP/SAVPF or SCTP/DTLS.
  std::string protocol() const { return protocol_; }
  void set_protocol(const std::string& protocol) { protocol_ = protocol; }

  MediaContentDirection direction() const { return direction_; }
  void set_direction(MediaContentDirection direction) {
    direction_ = direction;
  }

  bool rtcp_mux() const { return rtcp_mux_; }
  void set_rtcp_mux(bool mux) { rtcp_mux_ = mux; }

  int bandwidth() const { return bandwidth_; }
  void set_bandwidth(int bandwidth) { bandwidth_ = bandwidth; }

  const std::vector<CryptoParams>& cryptos() const { return cryptos_; }
  void AddCrypto(const CryptoParams& params) {
    cryptos_.push_back(params);
  }
  void set_cryptos(const std::vector<CryptoParams>& cryptos) {
    cryptos_ = cryptos;
  }

  CryptoType crypto_required() const { return crypto_required_; }
  void set_crypto_required(CryptoType type) {
    crypto_required_ = type;
  }

  const RtpHeaderExtensions& rtp_header_extensions() const {
    return rtp_header_extensions_;
  }
  void set_rtp_header_extensions(const RtpHeaderExtensions& extensions) {
    rtp_header_extensions_ = extensions;
    rtp_header_extensions_set_ = true;
  }
  void AddRtpHeaderExtension(const RtpHeaderExtension& ext) {
    rtp_header_extensions_.push_back(ext);
    rtp_header_extensions_set_ = true;
  }
  void ClearRtpHeaderExtensions() {
    rtp_header_extensions_.clear();
    rtp_header_extensions_set_ = true;
  }
  // We can't always tell if an empty list of header extensions is
  // because the other side doesn't support them, or just isn't hooked up to
  // signal them. For now we assume an empty list means no signaling, but
  // provide the ClearRtpHeaderExtensions method to allow "no support" to be
  // clearly indicated (i.e. when derived from other information).
  bool rtp_header_extensions_set() const {
    return rtp_header_extensions_set_;
  }
  // True iff the client supports multiple streams.
  void set_multistream(bool multistream) { multistream_ = multistream; }
  bool multistream() const { return multistream_; }
  const StreamParamsVec& streams() const {
    return streams_;
  }
  // TODO(pthatcher): Remove this by giving mediamessage.cc access
  // to MediaContentDescription
  StreamParamsVec& mutable_streams() {
    return streams_;
  }
  void AddStream(const StreamParams& stream) {
    streams_.push_back(stream);
  }
  // Legacy streams have an ssrc, but nothing else.
  void AddLegacyStream(uint32_t ssrc) {
    streams_.push_back(StreamParams::CreateLegacy(ssrc));
  }
  void AddLegacyStream(uint32_t ssrc, uint32_t fid_ssrc) {
    StreamParams sp = StreamParams::CreateLegacy(ssrc);
    sp.AddFidSsrc(ssrc, fid_ssrc);
    streams_.push_back(sp);
  }
  // Sets the CNAME of all StreamParams if it have not been set.
  // This can be used to set the CNAME of legacy streams.
  void SetCnameIfEmpty(const std::string& cname) {
    for (cricket::StreamParamsVec::iterator it = streams_.begin();
         it != streams_.end(); ++it) {
      if (it->cname.empty())
        it->cname = cname;
    }
  }
  uint32_t first_ssrc() const {
    if (streams_.empty()) {
      return 0;
    }
    return streams_[0].first_ssrc();
  }
  bool has_ssrcs() const {
    if (streams_.empty()) {
      return false;
    }
    return streams_[0].has_ssrcs();
  }

  void set_conference_mode(bool enable) { conference_mode_ = enable; }
  bool conference_mode() const { return conference_mode_; }

  void set_partial(bool partial) { partial_ = partial; }
  bool partial() const { return partial_;  }

  void set_buffered_mode_latency(int latency) {
    buffered_mode_latency_ = latency;
  }
  int buffered_mode_latency() const { return buffered_mode_latency_; }

 protected:
  bool rtcp_mux_;
  int bandwidth_;
  std::string protocol_;
  std::vector<CryptoParams> cryptos_;
  CryptoType crypto_required_;
  std::vector<RtpHeaderExtension> rtp_header_extensions_;
  bool rtp_header_extensions_set_;
  bool multistream_;
  StreamParamsVec streams_;
  bool conference_mode_;
  bool partial_;
  int buffered_mode_latency_;
  MediaContentDirection direction_;
};

template <class C>
class MediaContentDescriptionImpl : public MediaContentDescription {
 public:
  struct PreferenceSort {
    bool operator()(C a, C b) { return a.preference > b.preference; }
  };

  const std::vector<C>& codecs() const { return codecs_; }
  void set_codecs(const std::vector<C>& codecs) { codecs_ = codecs; }
  virtual bool has_codecs() const { return !codecs_.empty(); }
  bool HasCodec(int id) {
    bool found = false;
    for (typename std::vector<C>::iterator iter = codecs_.begin();
         iter != codecs_.end(); ++iter) {
      if (iter->id == id) {
        found = true;
        break;
      }
    }
    return found;
  }
  void AddCodec(const C& codec) {
    codecs_.push_back(codec);
  }
  void AddOrReplaceCodec(const C& codec) {
    for (typename std::vector<C>::iterator iter = codecs_.begin();
         iter != codecs_.end(); ++iter) {
      if (iter->id == codec.id) {
        *iter = codec;
        return;
      }
    }
    AddCodec(codec);
  }
  void AddCodecs(const std::vector<C>& codecs) {
    typename std::vector<C>::const_iterator codec;
    for (codec = codecs.begin(); codec != codecs.end(); ++codec) {
      AddCodec(*codec);
    }
  }
  void SortCodecs() {
    std::sort(codecs_.begin(), codecs_.end(), PreferenceSort());
  }

 private:
  std::vector<C> codecs_;
};

class AudioContentDescription : public MediaContentDescriptionImpl<AudioCodec> {
 public:
  AudioContentDescription() :
      agc_minus_10db_(false) {}

  virtual ContentDescription* Copy() const {
    return new AudioContentDescription(*this);
  }
  virtual MediaType type() const { return MEDIA_TYPE_AUDIO; }

  const std::string &lang() const { return lang_; }
  void set_lang(const std::string &lang) { lang_ = lang; }

  bool agc_minus_10db() const { return agc_minus_10db_; }
  void set_agc_minus_10db(bool enable) {
    agc_minus_10db_ = enable;
  }

 private:
  bool agc_minus_10db_;

 private:
  std::string lang_;
};

class VideoContentDescription : public MediaContentDescriptionImpl<VideoCodec> {
 public:
  virtual ContentDescription* Copy() const {
    return new VideoContentDescription(*this);
  }
  virtual MediaType type() const { return MEDIA_TYPE_VIDEO; }
};

class DataContentDescription : public MediaContentDescriptionImpl<DataCodec> {
 public:
  virtual ContentDescription* Copy() const {
    return new DataContentDescription(*this);
  }
  virtual MediaType type() const { return MEDIA_TYPE_DATA; }
};

// Creates media session descriptions according to the supplied codecs and
// other fields, as well as the supplied per-call options.
// When creating answers, performs the appropriate negotiation
// of the various fields to determine the proper result.
class MediaSessionDescriptionFactory {
 public:
  // Default ctor; use methods below to set configuration.
  // The TransportDescriptionFactory is not owned by MediaSessionDescFactory,
  // so it must be kept alive by the user of this class.
  explicit MediaSessionDescriptionFactory(
      const TransportDescriptionFactory* factory);
  // This helper automatically sets up the factory to get its configuration
  // from the specified ChannelManager.
  MediaSessionDescriptionFactory(ChannelManager* cmanager,
                                 const TransportDescriptionFactory* factory);

  const AudioCodecs& audio_codecs() const { return audio_codecs_; }
  void set_audio_codecs(const AudioCodecs& codecs) { audio_codecs_ = codecs; }
  void set_audio_rtp_header_extensions(const RtpHeaderExtensions& extensions) {
    audio_rtp_extensions_ = extensions;
  }
  const RtpHeaderExtensions& audio_rtp_header_extensions() const {
    return audio_rtp_extensions_;
  }
  const VideoCodecs& video_codecs() const { return video_codecs_; }
  void set_video_codecs(const VideoCodecs& codecs) { video_codecs_ = codecs; }
  void set_video_rtp_header_extensions(const RtpHeaderExtensions& extensions) {
    video_rtp_extensions_ = extensions;
  }
  const RtpHeaderExtensions& video_rtp_header_extensions() const {
    return video_rtp_extensions_;
  }
  const DataCodecs& data_codecs() const { return data_codecs_; }
  void set_data_codecs(const DataCodecs& codecs) { data_codecs_ = codecs; }
  SecurePolicy secure() const { return secure_; }
  void set_secure(SecurePolicy s) { secure_ = s; }
  // Decides if a StreamParams shall be added to the audio and video media
  // content in SessionDescription when CreateOffer and CreateAnswer is called
  // even if |options| don't include a Stream. This is needed to support legacy
  // applications. |add_legacy_| is true per default.
  void set_add_legacy_streams(bool add_legacy) { add_legacy_ = add_legacy; }

  SessionDescription* CreateOffer(
      const MediaSessionOptions& options,
      const SessionDescription* current_description) const;
  SessionDescription* CreateAnswer(
        const SessionDescription* offer,
        const MediaSessionOptions& options,
        const SessionDescription* current_description) const;

 private:
  void GetCodecsToOffer(const SessionDescription* current_description,
                        AudioCodecs* audio_codecs,
                        VideoCodecs* video_codecs,
                        DataCodecs* data_codecs) const;
  void GetRtpHdrExtsToOffer(const SessionDescription* current_description,
                            RtpHeaderExtensions* audio_extensions,
                            RtpHeaderExtensions* video_extensions) const;
  bool AddTransportOffer(
      const std::string& content_name,
      const TransportOptions& transport_options,
      const SessionDescription* current_desc,
      SessionDescription* offer) const;

  TransportDescription* CreateTransportAnswer(
      const std::string& content_name,
      const SessionDescription* offer_desc,
      const TransportOptions& transport_options,
      const SessionDescription* current_desc) const;

  bool AddTransportAnswer(
      const std::string& content_name,
      const TransportDescription& transport_desc,
      SessionDescription* answer_desc) const;

  // Helpers for adding media contents to the SessionDescription. Returns true
  // it succeeds or the media content is not needed, or false if there is any
  // error.

  bool AddAudioContentForOffer(
      const MediaSessionOptions& options,
      const SessionDescription* current_description,
      const RtpHeaderExtensions& audio_rtp_extensions,
      const AudioCodecs& audio_codecs,
      StreamParamsVec* current_streams,
      SessionDescription* desc) const;

  bool AddVideoContentForOffer(
      const MediaSessionOptions& options,
      const SessionDescription* current_description,
      const RtpHeaderExtensions& video_rtp_extensions,
      const VideoCodecs& video_codecs,
      StreamParamsVec* current_streams,
      SessionDescription* desc) const;

  bool AddDataContentForOffer(
      const MediaSessionOptions& options,
      const SessionDescription* current_description,
      DataCodecs* data_codecs,
      StreamParamsVec* current_streams,
      SessionDescription* desc) const;

  bool AddAudioContentForAnswer(
      const SessionDescription* offer,
      const MediaSessionOptions& options,
      const SessionDescription* current_description,
      StreamParamsVec* current_streams,
      SessionDescription* answer) const;

  bool AddVideoContentForAnswer(
      const SessionDescription* offer,
      const MediaSessionOptions& options,
      const SessionDescription* current_description,
      StreamParamsVec* current_streams,
      SessionDescription* answer) const;

  bool AddDataContentForAnswer(
      const SessionDescription* offer,
      const MediaSessionOptions& options,
      const SessionDescription* current_description,
      StreamParamsVec* current_streams,
      SessionDescription* answer) const;

  AudioCodecs audio_codecs_;
  RtpHeaderExtensions audio_rtp_extensions_;
  VideoCodecs video_codecs_;
  RtpHeaderExtensions video_rtp_extensions_;
  DataCodecs data_codecs_;
  SecurePolicy secure_;
  bool add_legacy_;
  std::string lang_;
  const TransportDescriptionFactory* transport_desc_factory_;
};

// Convenience functions.
bool IsMediaContent(const ContentInfo* content);
bool IsAudioContent(const ContentInfo* content);
bool IsVideoContent(const ContentInfo* content);
bool IsDataContent(const ContentInfo* content);
const ContentInfo* GetFirstAudioContent(const ContentInfos& contents);
const ContentInfo* GetFirstVideoContent(const ContentInfos& contents);
const ContentInfo* GetFirstDataContent(const ContentInfos& contents);
const ContentInfo* GetFirstAudioContent(const SessionDescription* sdesc);
const ContentInfo* GetFirstVideoContent(const SessionDescription* sdesc);
const ContentInfo* GetFirstDataContent(const SessionDescription* sdesc);
const AudioContentDescription* GetFirstAudioContentDescription(
    const SessionDescription* sdesc);
const VideoContentDescription* GetFirstVideoContentDescription(
    const SessionDescription* sdesc);
const DataContentDescription* GetFirstDataContentDescription(
    const SessionDescription* sdesc);

void GetSupportedAudioCryptoSuites(std::vector<int>* crypto_suites);
void GetSupportedVideoCryptoSuites(std::vector<int>* crypto_suites);
void GetSupportedDataCryptoSuites(std::vector<int>* crypto_suites);
void GetDefaultSrtpCryptoSuites(std::vector<int>* crypto_suites);
void GetSupportedAudioCryptoSuiteNames(
    std::vector<std::string>* crypto_suite_names);
void GetSupportedVideoCryptoSuiteNames(
    std::vector<std::string>* crypto_suite_names);
void GetSupportedDataCryptoSuiteNames(
    std::vector<std::string>* crypto_suite_names);
void GetDefaultSrtpCryptoSuiteNames(
    std::vector<std::string>* crypto_suite_names);

}  // namespace cricket

#endif  // TALK_SESSION_MEDIA_MEDIASESSION_H_
