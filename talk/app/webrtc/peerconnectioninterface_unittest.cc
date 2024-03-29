/*
 * libjingle
 * Copyright 2012 Google Inc.
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

#include <string>

#include "talk/app/webrtc/audiotrack.h"
#include "talk/app/webrtc/fakeportallocatorfactory.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/mediastream.h"
#include "talk/app/webrtc/mediastreaminterface.h"
#include "talk/app/webrtc/peerconnection.h"
#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/app/webrtc/rtpreceiverinterface.h"
#include "talk/app/webrtc/rtpsenderinterface.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/app/webrtc/test/fakedtlsidentitystore.h"
#include "talk/app/webrtc/test/mockpeerconnectionobservers.h"
#include "talk/app/webrtc/test/testsdpstrings.h"
#include "talk/app/webrtc/videosource.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/media/base/fakevideocapturer.h"
#include "talk/media/sctp/sctpdataengine.h"
#include "talk/session/media/mediasession.h"
#include "webrtc/base/gunit.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/ssladapter.h"
#include "webrtc/base/sslstreamadapter.h"
#include "webrtc/base/stringutils.h"
#include "webrtc/base/thread.h"

static const char kStreamLabel1[] = "local_stream_1";
static const char kStreamLabel2[] = "local_stream_2";
static const char kStreamLabel3[] = "local_stream_3";
static const int kDefaultStunPort = 3478;
static const char kStunAddressOnly[] = "stun:address";
static const char kStunInvalidPort[] = "stun:address:-1";
static const char kStunAddressPortAndMore1[] = "stun:address:port:more";
static const char kStunAddressPortAndMore2[] = "stun:address:port more";
static const char kTurnIceServerUri[] = "turn:user@turn.example.org";
static const char kTurnUsername[] = "user";
static const char kTurnPassword[] = "password";
static const char kTurnHostname[] = "turn.example.org";
static const uint32_t kTimeout = 10000U;

static const char kStreams[][8] = {"stream1", "stream2"};
static const char kAudioTracks[][32] = {"audiotrack0", "audiotrack1"};
static const char kVideoTracks[][32] = {"videotrack0", "videotrack1"};

static const char kRecvonly[] = "recvonly";
static const char kSendrecv[] = "sendrecv";

// Reference SDP with a MediaStream with label "stream1" and audio track with
// id "audio_1" and a video track with id "video_1;
static const char kSdpStringWithStream1[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 mslabel:stream1\r\n"
    "a=ssrc:1 label:audiotrack0\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n"
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 mslabel:stream1\r\n"
    "a=ssrc:2 label:videotrack0\r\n";

// Reference SDP with two MediaStreams with label "stream1" and "stream2. Each
// MediaStreams have one audio track and one video track.
// This uses MSID.
static const char kSdpStringWithStream1And2[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "a=msid-semantic: WMS stream1 stream2\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 msid:stream1 audiotrack0\r\n"
    "a=ssrc:3 cname:stream2\r\n"
    "a=ssrc:3 msid:stream2 audiotrack1\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/0\r\n"
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 msid:stream1 videotrack0\r\n"
    "a=ssrc:4 cname:stream2\r\n"
    "a=ssrc:4 msid:stream2 videotrack1\r\n";

// Reference SDP without MediaStreams. Msid is not supported.
static const char kSdpStringWithoutStreams[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

// Reference SDP without MediaStreams. Msid is supported.
static const char kSdpStringWithMsidWithoutStreams[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "a=msid-semantic: WMS\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

// Reference SDP without MediaStreams and audio only.
static const char kSdpStringWithoutStreamsAudioOnly[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:103 ISAC/16000\r\n";

// Reference SENDONLY SDP without MediaStreams. Msid is not supported.
static const char kSdpStringSendOnlyWithoutStreams[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=sendonly\r\n"
    "a=rtpmap:103 ISAC/16000\r\n"
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=sendonly\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

static const char kSdpStringInit[] =
    "v=0\r\n"
    "o=- 0 0 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=ice-ufrag:e5785931\r\n"
    "a=ice-pwd:36fb7878390db89481c1d46daa4278d8\r\n"
    "a=fingerprint:sha-256 58:AB:6E:F5:F1:E4:57:B7:E9:46:F4:86:04:28:F9:A7:ED:"
    "BD:AB:AE:40:EF:CE:9A:51:2C:2A:B1:9B:8B:78:84\r\n"
    "a=msid-semantic: WMS\r\n";

static const char kSdpStringAudio[] =
    "m=audio 1 RTP/AVPF 103\r\n"
    "a=mid:audio\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:103 ISAC/16000\r\n";

static const char kSdpStringVideo[] =
    "m=video 1 RTP/AVPF 120\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=rtpmap:120 VP8/90000\r\n";

static const char kSdpStringMs1Audio0[] =
    "a=ssrc:1 cname:stream1\r\n"
    "a=ssrc:1 msid:stream1 audiotrack0\r\n";

static const char kSdpStringMs1Video0[] =
    "a=ssrc:2 cname:stream1\r\n"
    "a=ssrc:2 msid:stream1 videotrack0\r\n";

static const char kSdpStringMs1Audio1[] =
    "a=ssrc:3 cname:stream1\r\n"
    "a=ssrc:3 msid:stream1 audiotrack1\r\n";

static const char kSdpStringMs1Video1[] =
    "a=ssrc:4 cname:stream1\r\n"
    "a=ssrc:4 msid:stream1 videotrack1\r\n";

#define MAYBE_SKIP_TEST(feature)                    \
  if (!(feature())) {                               \
    LOG(LS_INFO) << "Feature disabled... skipping"; \
    return;                                         \
  }

using rtc::scoped_ptr;
using rtc::scoped_refptr;
using webrtc::AudioSourceInterface;
using webrtc::AudioTrack;
using webrtc::AudioTrackInterface;
using webrtc::DataBuffer;
using webrtc::DataChannelInterface;
using webrtc::FakeConstraints;
using webrtc::FakePortAllocatorFactory;
using webrtc::IceCandidateInterface;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaStream;
using webrtc::MediaStreamInterface;
using webrtc::MediaStreamTrackInterface;
using webrtc::MockCreateSessionDescriptionObserver;
using webrtc::MockDataChannelObserver;
using webrtc::MockSetSessionDescriptionObserver;
using webrtc::MockStatsObserver;
using webrtc::PeerConnectionInterface;
using webrtc::PeerConnectionObserver;
using webrtc::PortAllocatorFactoryInterface;
using webrtc::RtpReceiverInterface;
using webrtc::RtpSenderInterface;
using webrtc::SdpParseError;
using webrtc::SessionDescriptionInterface;
using webrtc::StreamCollection;
using webrtc::StreamCollectionInterface;
using webrtc::VideoSourceInterface;
using webrtc::VideoTrack;
using webrtc::VideoTrackInterface;

typedef PeerConnectionInterface::RTCOfferAnswerOptions RTCOfferAnswerOptions;

namespace {

// Gets the first ssrc of given content type from the ContentInfo.
bool GetFirstSsrc(const cricket::ContentInfo* content_info, int* ssrc) {
  if (!content_info || !ssrc) {
    return false;
  }
  const cricket::MediaContentDescription* media_desc =
      static_cast<const cricket::MediaContentDescription*>(
          content_info->description);
  if (!media_desc || media_desc->streams().empty()) {
    return false;
  }
  *ssrc = media_desc->streams().begin()->first_ssrc();
  return true;
}

void SetSsrcToZero(std::string* sdp) {
  const char kSdpSsrcAtribute[] = "a=ssrc:";
  const char kSdpSsrcAtributeZero[] = "a=ssrc:0";
  size_t ssrc_pos = 0;
  while ((ssrc_pos = sdp->find(kSdpSsrcAtribute, ssrc_pos)) !=
      std::string::npos) {
    size_t end_ssrc = sdp->find(" ", ssrc_pos);
    sdp->replace(ssrc_pos, end_ssrc - ssrc_pos, kSdpSsrcAtributeZero);
    ssrc_pos = end_ssrc;
  }
}

// Check if |streams| contains the specified track.
bool ContainsTrack(const std::vector<cricket::StreamParams>& streams,
                   const std::string& stream_label,
                   const std::string& track_id) {
  for (const cricket::StreamParams& params : streams) {
    if (params.sync_label == stream_label && params.id == track_id) {
      return true;
    }
  }
  return false;
}

// Check if |senders| contains the specified sender, by id.
bool ContainsSender(
    const std::vector<rtc::scoped_refptr<RtpSenderInterface>>& senders,
    const std::string& id) {
  for (const auto& sender : senders) {
    if (sender->id() == id) {
      return true;
    }
  }
  return false;
}

// Create a collection of streams.
// CreateStreamCollection(1) creates a collection that
// correspond to kSdpStringWithStream1.
// CreateStreamCollection(2) correspond to kSdpStringWithStream1And2.
rtc::scoped_refptr<StreamCollection> CreateStreamCollection(
    int number_of_streams) {
  rtc::scoped_refptr<StreamCollection> local_collection(
      StreamCollection::Create());

  for (int i = 0; i < number_of_streams; ++i) {
    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream(
        webrtc::MediaStream::Create(kStreams[i]));

    // Add a local audio track.
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
        webrtc::AudioTrack::Create(kAudioTracks[i], nullptr));
    stream->AddTrack(audio_track);

    // Add a local video track.
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
        webrtc::VideoTrack::Create(kVideoTracks[i], nullptr));
    stream->AddTrack(video_track);

    local_collection->AddStream(stream);
  }
  return local_collection;
}

// Check equality of StreamCollections.
bool CompareStreamCollections(StreamCollectionInterface* s1,
                              StreamCollectionInterface* s2) {
  if (s1 == nullptr || s2 == nullptr || s1->count() != s2->count()) {
    return false;
  }

  for (size_t i = 0; i != s1->count(); ++i) {
    if (s1->at(i)->label() != s2->at(i)->label()) {
      return false;
    }
    webrtc::AudioTrackVector audio_tracks1 = s1->at(i)->GetAudioTracks();
    webrtc::AudioTrackVector audio_tracks2 = s2->at(i)->GetAudioTracks();
    webrtc::VideoTrackVector video_tracks1 = s1->at(i)->GetVideoTracks();
    webrtc::VideoTrackVector video_tracks2 = s2->at(i)->GetVideoTracks();

    if (audio_tracks1.size() != audio_tracks2.size()) {
      return false;
    }
    for (size_t j = 0; j != audio_tracks1.size(); ++j) {
      if (audio_tracks1[j]->id() != audio_tracks2[j]->id()) {
        return false;
      }
    }
    if (video_tracks1.size() != video_tracks2.size()) {
      return false;
    }
    for (size_t j = 0; j != video_tracks1.size(); ++j) {
      if (video_tracks1[j]->id() != video_tracks2[j]->id()) {
        return false;
      }
    }
  }
  return true;
}

class MockPeerConnectionObserver : public PeerConnectionObserver {
 public:
  MockPeerConnectionObserver() : remote_streams_(StreamCollection::Create()) {}
  ~MockPeerConnectionObserver() {
  }
  void SetPeerConnectionInterface(PeerConnectionInterface* pc) {
    pc_ = pc;
    if (pc) {
      state_ = pc_->signaling_state();
    }
  }
  virtual void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) {
    EXPECT_EQ(pc_->signaling_state(), new_state);
    state_ = new_state;
  }
  // TODO(bemasc): Remove this once callers transition to OnIceGatheringChange.
  virtual void OnStateChange(StateType state_changed) {
    if (pc_.get() == NULL)
      return;
    switch (state_changed) {
      case kSignalingState:
        // OnSignalingChange and OnStateChange(kSignalingState) should always
        // be called approximately simultaneously.  To ease testing, we require
        // that they always be called in that order.  This check verifies
        // that OnSignalingChange has just been called.
        EXPECT_EQ(pc_->signaling_state(), state_);
        break;
      case kIceState:
        ADD_FAILURE();
        break;
      default:
        ADD_FAILURE();
        break;
    }
  }

  MediaStreamInterface* RemoteStream(const std::string& label) {
    return remote_streams_->find(label);
  }
  StreamCollectionInterface* remote_streams() const { return remote_streams_; }
  virtual void OnAddStream(MediaStreamInterface* stream) {
    last_added_stream_ = stream;
    remote_streams_->AddStream(stream);
  }
  virtual void OnRemoveStream(MediaStreamInterface* stream) {
    last_removed_stream_ = stream;
    remote_streams_->RemoveStream(stream);
  }
  virtual void OnRenegotiationNeeded() {
    renegotiation_needed_ = true;
  }
  virtual void OnDataChannel(DataChannelInterface* data_channel) {
    last_datachannel_ = data_channel;
  }

  virtual void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) {
    EXPECT_EQ(pc_->ice_connection_state(), new_state);
  }
  virtual void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) {
    EXPECT_EQ(pc_->ice_gathering_state(), new_state);
  }
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    EXPECT_NE(PeerConnectionInterface::kIceGatheringNew,
              pc_->ice_gathering_state());

    std::string sdp;
    EXPECT_TRUE(candidate->ToString(&sdp));
    EXPECT_LT(0u, sdp.size());
    last_candidate_.reset(webrtc::CreateIceCandidate(candidate->sdp_mid(),
        candidate->sdp_mline_index(), sdp, NULL));
    EXPECT_TRUE(last_candidate_.get() != NULL);
  }
  // TODO(bemasc): Remove this once callers transition to OnSignalingChange.
  virtual void OnIceComplete() {
    ice_complete_ = true;
    // OnIceGatheringChange(IceGatheringCompleted) and OnIceComplete() should
    // be called approximately simultaneously.  For ease of testing, this
    // check additionally requires that they be called in the above order.
    EXPECT_EQ(PeerConnectionInterface::kIceGatheringComplete,
      pc_->ice_gathering_state());
  }

  // Returns the label of the last added stream.
  // Empty string if no stream have been added.
  std::string GetLastAddedStreamLabel() {
    if (last_added_stream_.get())
      return last_added_stream_->label();
    return "";
  }
  std::string GetLastRemovedStreamLabel() {
    if (last_removed_stream_.get())
      return last_removed_stream_->label();
    return "";
  }

  scoped_refptr<PeerConnectionInterface> pc_;
  PeerConnectionInterface::SignalingState state_;
  scoped_ptr<IceCandidateInterface> last_candidate_;
  scoped_refptr<DataChannelInterface> last_datachannel_;
  rtc::scoped_refptr<StreamCollection> remote_streams_;
  bool renegotiation_needed_ = false;
  bool ice_complete_ = false;

 private:
  scoped_refptr<MediaStreamInterface> last_added_stream_;
  scoped_refptr<MediaStreamInterface> last_removed_stream_;
};

}  // namespace

class PeerConnectionInterfaceTest : public testing::Test {
 protected:
  virtual void SetUp() {
    pc_factory_ = webrtc::CreatePeerConnectionFactory(
        rtc::Thread::Current(), rtc::Thread::Current(), NULL, NULL,
        NULL);
    ASSERT_TRUE(pc_factory_.get() != NULL);
  }

  void CreatePeerConnection() {
    CreatePeerConnection("", "", NULL);
  }

  void CreatePeerConnection(webrtc::MediaConstraintsInterface* constraints) {
    CreatePeerConnection("", "", constraints);
  }

  void CreatePeerConnection(const std::string& uri,
                            const std::string& password,
                            webrtc::MediaConstraintsInterface* constraints) {
    PeerConnectionInterface::IceServer server;
    PeerConnectionInterface::IceServers servers;
    if (!uri.empty()) {
      server.uri = uri;
      server.password = password;
      servers.push_back(server);
    }

    port_allocator_factory_ = FakePortAllocatorFactory::Create();

    // DTLS does not work in a loopback call, so is disabled for most of the
    // tests in this file. We only create a FakeIdentityService if the test
    // explicitly sets the constraint.
    FakeConstraints default_constraints;
    if (!constraints) {
      constraints = &default_constraints;

      default_constraints.AddMandatory(
          webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, false);
    }

    scoped_ptr<webrtc::DtlsIdentityStoreInterface> dtls_identity_store;
    bool dtls;
    if (FindConstraint(constraints,
                       webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                       &dtls,
                       nullptr) && dtls) {
      dtls_identity_store.reset(new FakeDtlsIdentityStore());
    }
    pc_ = pc_factory_->CreatePeerConnection(servers, constraints,
                                            port_allocator_factory_.get(),
                                            dtls_identity_store.Pass(),
                                            &observer_);
    ASSERT_TRUE(pc_.get() != NULL);
    observer_.SetPeerConnectionInterface(pc_.get());
    EXPECT_EQ(PeerConnectionInterface::kStable, observer_.state_);
  }

  void CreatePeerConnectionExpectFail(const std::string& uri) {
    PeerConnectionInterface::IceServer server;
    PeerConnectionInterface::IceServers servers;
    server.uri = uri;
    servers.push_back(server);

    scoped_ptr<webrtc::DtlsIdentityStoreInterface> dtls_identity_store;
    port_allocator_factory_ = FakePortAllocatorFactory::Create();
    scoped_refptr<PeerConnectionInterface> pc;
    pc = pc_factory_->CreatePeerConnection(
        servers, nullptr, port_allocator_factory_.get(),
        dtls_identity_store.Pass(), &observer_);
    ASSERT_EQ(nullptr, pc);
  }

  void CreatePeerConnectionWithDifferentConfigurations() {
    CreatePeerConnection(kStunAddressOnly, "", NULL);
    EXPECT_EQ(1u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(0u, port_allocator_factory_->turn_configs().size());
    EXPECT_EQ("address",
        port_allocator_factory_->stun_configs()[0].server.hostname());
    EXPECT_EQ(kDefaultStunPort,
        port_allocator_factory_->stun_configs()[0].server.port());

    CreatePeerConnectionExpectFail(kStunInvalidPort);
    CreatePeerConnectionExpectFail(kStunAddressPortAndMore1);
    CreatePeerConnectionExpectFail(kStunAddressPortAndMore2);

    CreatePeerConnection(kTurnIceServerUri, kTurnPassword, NULL);
    EXPECT_EQ(0u, port_allocator_factory_->stun_configs().size());
    EXPECT_EQ(1u, port_allocator_factory_->turn_configs().size());
    EXPECT_EQ(kTurnUsername,
              port_allocator_factory_->turn_configs()[0].username);
    EXPECT_EQ(kTurnPassword,
              port_allocator_factory_->turn_configs()[0].password);
    EXPECT_EQ(kTurnHostname,
              port_allocator_factory_->turn_configs()[0].server.hostname());
  }

  void ReleasePeerConnection() {
    pc_ = NULL;
    observer_.SetPeerConnectionInterface(NULL);
  }

  void AddVideoStream(const std::string& label) {
    // Create a local stream.
    scoped_refptr<MediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(label));
    scoped_refptr<VideoSourceInterface> video_source(
        pc_factory_->CreateVideoSource(new cricket::FakeVideoCapturer(), NULL));
    scoped_refptr<VideoTrackInterface> video_track(
        pc_factory_->CreateVideoTrack(label + "v0", video_source));
    stream->AddTrack(video_track.get());
    EXPECT_TRUE(pc_->AddStream(stream));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  void AddVoiceStream(const std::string& label) {
    // Create a local stream.
    scoped_refptr<MediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(label));
    scoped_refptr<AudioTrackInterface> audio_track(
        pc_factory_->CreateAudioTrack(label + "a0", NULL));
    stream->AddTrack(audio_track.get());
    EXPECT_TRUE(pc_->AddStream(stream));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  void AddAudioVideoStream(const std::string& stream_label,
                           const std::string& audio_track_label,
                           const std::string& video_track_label) {
    // Create a local stream.
    scoped_refptr<MediaStreamInterface> stream(
        pc_factory_->CreateLocalMediaStream(stream_label));
    scoped_refptr<AudioTrackInterface> audio_track(
        pc_factory_->CreateAudioTrack(
            audio_track_label, static_cast<AudioSourceInterface*>(NULL)));
    stream->AddTrack(audio_track.get());
    scoped_refptr<VideoTrackInterface> video_track(
        pc_factory_->CreateVideoTrack(video_track_label, NULL));
    stream->AddTrack(video_track.get());
    EXPECT_TRUE(pc_->AddStream(stream));
    EXPECT_TRUE_WAIT(observer_.renegotiation_needed_, kTimeout);
    observer_.renegotiation_needed_ = false;
  }

  bool DoCreateOfferAnswer(SessionDescriptionInterface** desc,
                           bool offer,
                           MediaConstraintsInterface* constraints) {
    rtc::scoped_refptr<MockCreateSessionDescriptionObserver>
        observer(new rtc::RefCountedObject<
            MockCreateSessionDescriptionObserver>());
    if (offer) {
      pc_->CreateOffer(observer, constraints);
    } else {
      pc_->CreateAnswer(observer, constraints);
    }
    EXPECT_EQ_WAIT(true, observer->called(), kTimeout);
    *desc = observer->release_desc();
    return observer->result();
  }

  bool DoCreateOffer(SessionDescriptionInterface** desc,
                     MediaConstraintsInterface* constraints) {
    return DoCreateOfferAnswer(desc, true, constraints);
  }

  bool DoCreateAnswer(SessionDescriptionInterface** desc,
                      MediaConstraintsInterface* constraints) {
    return DoCreateOfferAnswer(desc, false, constraints);
  }

  bool DoSetSessionDescription(SessionDescriptionInterface* desc, bool local) {
    rtc::scoped_refptr<MockSetSessionDescriptionObserver>
        observer(new rtc::RefCountedObject<
            MockSetSessionDescriptionObserver>());
    if (local) {
      pc_->SetLocalDescription(observer, desc);
    } else {
      pc_->SetRemoteDescription(observer, desc);
    }
    EXPECT_EQ_WAIT(true, observer->called(), kTimeout);
    return observer->result();
  }

  bool DoSetLocalDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, true);
  }

  bool DoSetRemoteDescription(SessionDescriptionInterface* desc) {
    return DoSetSessionDescription(desc, false);
  }

  // Calls PeerConnection::GetStats and check the return value.
  // It does not verify the values in the StatReports since a RTCP packet might
  // be required.
  bool DoGetStats(MediaStreamTrackInterface* track) {
    rtc::scoped_refptr<MockStatsObserver> observer(
        new rtc::RefCountedObject<MockStatsObserver>());
    if (!pc_->GetStats(
        observer, track, PeerConnectionInterface::kStatsOutputLevelStandard))
      return false;
    EXPECT_TRUE_WAIT(observer->called(), kTimeout);
    return observer->called();
  }

  void InitiateCall() {
    CreatePeerConnection();
    // Create a local stream with audio&video tracks.
    AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");
    CreateOfferReceiveAnswer();
  }

  // Verify that RTP Header extensions has been negotiated for audio and video.
  void VerifyRemoteRtpHeaderExtensions() {
    const cricket::MediaContentDescription* desc =
        cricket::GetFirstAudioContentDescription(
            pc_->remote_description()->description());
    ASSERT_TRUE(desc != NULL);
    EXPECT_GT(desc->rtp_header_extensions().size(), 0u);

    desc = cricket::GetFirstVideoContentDescription(
        pc_->remote_description()->description());
    ASSERT_TRUE(desc != NULL);
    EXPECT_GT(desc->rtp_header_extensions().size(), 0u);
  }

  void CreateOfferAsRemoteDescription() {
    rtc::scoped_ptr<SessionDescriptionInterface> offer;
    ASSERT_TRUE(DoCreateOffer(offer.use(), nullptr));
    std::string sdp;
    EXPECT_TRUE(offer->ToString(&sdp));
    SessionDescriptionInterface* remote_offer =
        webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                         sdp, NULL);
    EXPECT_TRUE(DoSetRemoteDescription(remote_offer));
    EXPECT_EQ(PeerConnectionInterface::kHaveRemoteOffer, observer_.state_);
  }

  void CreateAndSetRemoteOffer(const std::string& sdp) {
    SessionDescriptionInterface* remote_offer =
        webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                         sdp, nullptr);
    EXPECT_TRUE(DoSetRemoteDescription(remote_offer));
    EXPECT_EQ(PeerConnectionInterface::kHaveRemoteOffer, observer_.state_);
  }

  void CreateAnswerAsLocalDescription() {
    scoped_ptr<SessionDescriptionInterface> answer;
    ASSERT_TRUE(DoCreateAnswer(answer.use(), nullptr));

    // TODO(perkj): Currently SetLocalDescription fails if any parameters in an
    // audio codec change, even if the parameter has nothing to do with
    // receiving. Not all parameters are serialized to SDP.
    // Since CreatePrAnswerAsLocalDescription serialize/deserialize
    // the SessionDescription, it is necessary to do that here to in order to
    // get ReceiveOfferCreatePrAnswerAndAnswer and RenegotiateAudioOnly to pass.
    // https://code.google.com/p/webrtc/issues/detail?id=1356
    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));
    SessionDescriptionInterface* new_answer =
        webrtc::CreateSessionDescription(SessionDescriptionInterface::kAnswer,
                                         sdp, NULL);
    EXPECT_TRUE(DoSetLocalDescription(new_answer));
    EXPECT_EQ(PeerConnectionInterface::kStable, observer_.state_);
  }

  void CreatePrAnswerAsLocalDescription() {
    scoped_ptr<SessionDescriptionInterface> answer;
    ASSERT_TRUE(DoCreateAnswer(answer.use(), nullptr));

    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));
    SessionDescriptionInterface* pr_answer =
        webrtc::CreateSessionDescription(SessionDescriptionInterface::kPrAnswer,
                                         sdp, NULL);
    EXPECT_TRUE(DoSetLocalDescription(pr_answer));
    EXPECT_EQ(PeerConnectionInterface::kHaveLocalPrAnswer, observer_.state_);
  }

  void CreateOfferReceiveAnswer() {
    CreateOfferAsLocalDescription();
    std::string sdp;
    EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
    CreateAnswerAsRemoteDescription(sdp);
  }

  void CreateOfferAsLocalDescription() {
    rtc::scoped_ptr<SessionDescriptionInterface> offer;
    ASSERT_TRUE(DoCreateOffer(offer.use(), nullptr));
    // TODO(perkj): Currently SetLocalDescription fails if any parameters in an
    // audio codec change, even if the parameter has nothing to do with
    // receiving. Not all parameters are serialized to SDP.
    // Since CreatePrAnswerAsLocalDescription serialize/deserialize
    // the SessionDescription, it is necessary to do that here to in order to
    // get ReceiveOfferCreatePrAnswerAndAnswer and RenegotiateAudioOnly to pass.
    // https://code.google.com/p/webrtc/issues/detail?id=1356
    std::string sdp;
    EXPECT_TRUE(offer->ToString(&sdp));
    SessionDescriptionInterface* new_offer =
            webrtc::CreateSessionDescription(
                SessionDescriptionInterface::kOffer,
                sdp, NULL);

    EXPECT_TRUE(DoSetLocalDescription(new_offer));
    EXPECT_EQ(PeerConnectionInterface::kHaveLocalOffer, observer_.state_);
    // Wait for the ice_complete message, so that SDP will have candidates.
    EXPECT_TRUE_WAIT(observer_.ice_complete_, kTimeout);
  }

  void CreateAnswerAsRemoteDescription(const std::string& sdp) {
    webrtc::JsepSessionDescription* answer = new webrtc::JsepSessionDescription(
        SessionDescriptionInterface::kAnswer);
    EXPECT_TRUE(answer->Initialize(sdp, NULL));
    EXPECT_TRUE(DoSetRemoteDescription(answer));
    EXPECT_EQ(PeerConnectionInterface::kStable, observer_.state_);
  }

  void CreatePrAnswerAndAnswerAsRemoteDescription(const std::string& sdp) {
    webrtc::JsepSessionDescription* pr_answer =
        new webrtc::JsepSessionDescription(
            SessionDescriptionInterface::kPrAnswer);
    EXPECT_TRUE(pr_answer->Initialize(sdp, NULL));
    EXPECT_TRUE(DoSetRemoteDescription(pr_answer));
    EXPECT_EQ(PeerConnectionInterface::kHaveRemotePrAnswer, observer_.state_);
    webrtc::JsepSessionDescription* answer =
        new webrtc::JsepSessionDescription(
            SessionDescriptionInterface::kAnswer);
    EXPECT_TRUE(answer->Initialize(sdp, NULL));
    EXPECT_TRUE(DoSetRemoteDescription(answer));
    EXPECT_EQ(PeerConnectionInterface::kStable, observer_.state_);
  }

  // Help function used for waiting until a the last signaled remote stream has
  // the same label as |stream_label|. In a few of the tests in this file we
  // answer with the same session description as we offer and thus we can
  // check if OnAddStream have been called with the same stream as we offer to
  // send.
  void WaitAndVerifyOnAddStream(const std::string& stream_label) {
    EXPECT_EQ_WAIT(stream_label, observer_.GetLastAddedStreamLabel(), kTimeout);
  }

  // Creates an offer and applies it as a local session description.
  // Creates an answer with the same SDP an the offer but removes all lines
  // that start with a:ssrc"
  void CreateOfferReceiveAnswerWithoutSsrc() {
    CreateOfferAsLocalDescription();
    std::string sdp;
    EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
    SetSsrcToZero(&sdp);
    CreateAnswerAsRemoteDescription(sdp);
  }

  // This function creates a MediaStream with label kStreams[0] and
  // |number_of_audio_tracks| and |number_of_video_tracks| tracks and the
  // corresponding SessionDescriptionInterface. The SessionDescriptionInterface
  // is returned in |desc| and the MediaStream is stored in
  // |reference_collection_|
  void CreateSessionDescriptionAndReference(
      size_t number_of_audio_tracks,
      size_t number_of_video_tracks,
      SessionDescriptionInterface** desc) {
    ASSERT_TRUE(desc != nullptr);
    ASSERT_LE(number_of_audio_tracks, 2u);
    ASSERT_LE(number_of_video_tracks, 2u);

    reference_collection_ = StreamCollection::Create();
    std::string sdp_ms1 = std::string(kSdpStringInit);

    std::string mediastream_label = kStreams[0];

    rtc::scoped_refptr<webrtc::MediaStreamInterface> stream(
        webrtc::MediaStream::Create(mediastream_label));
    reference_collection_->AddStream(stream);

    if (number_of_audio_tracks > 0) {
      sdp_ms1 += std::string(kSdpStringAudio);
      sdp_ms1 += std::string(kSdpStringMs1Audio0);
      AddAudioTrack(kAudioTracks[0], stream);
    }
    if (number_of_audio_tracks > 1) {
      sdp_ms1 += kSdpStringMs1Audio1;
      AddAudioTrack(kAudioTracks[1], stream);
    }

    if (number_of_video_tracks > 0) {
      sdp_ms1 += std::string(kSdpStringVideo);
      sdp_ms1 += std::string(kSdpStringMs1Video0);
      AddVideoTrack(kVideoTracks[0], stream);
    }
    if (number_of_video_tracks > 1) {
      sdp_ms1 += kSdpStringMs1Video1;
      AddVideoTrack(kVideoTracks[1], stream);
    }

    *desc = webrtc::CreateSessionDescription(
        SessionDescriptionInterface::kOffer, sdp_ms1, nullptr);
  }

  void AddAudioTrack(const std::string& track_id,
                     MediaStreamInterface* stream) {
    rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
        webrtc::AudioTrack::Create(track_id, nullptr));
    ASSERT_TRUE(stream->AddTrack(audio_track));
  }

  void AddVideoTrack(const std::string& track_id,
                     MediaStreamInterface* stream) {
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(
        webrtc::VideoTrack::Create(track_id, nullptr));
    ASSERT_TRUE(stream->AddTrack(video_track));
  }

  scoped_refptr<FakePortAllocatorFactory> port_allocator_factory_;
  scoped_refptr<webrtc::PeerConnectionFactoryInterface> pc_factory_;
  scoped_refptr<PeerConnectionInterface> pc_;
  MockPeerConnectionObserver observer_;
  rtc::scoped_refptr<StreamCollection> reference_collection_;
};

TEST_F(PeerConnectionInterfaceTest,
       CreatePeerConnectionWithDifferentConfigurations) {
  CreatePeerConnectionWithDifferentConfigurations();
}

TEST_F(PeerConnectionInterfaceTest, AddStreams) {
  CreatePeerConnection();
  AddVideoStream(kStreamLabel1);
  AddVoiceStream(kStreamLabel2);
  ASSERT_EQ(2u, pc_->local_streams()->count());

  // Test we can add multiple local streams to one peerconnection.
  scoped_refptr<MediaStreamInterface> stream(
      pc_factory_->CreateLocalMediaStream(kStreamLabel3));
  scoped_refptr<AudioTrackInterface> audio_track(
      pc_factory_->CreateAudioTrack(
          kStreamLabel3, static_cast<AudioSourceInterface*>(NULL)));
  stream->AddTrack(audio_track.get());
  EXPECT_TRUE(pc_->AddStream(stream));
  EXPECT_EQ(3u, pc_->local_streams()->count());

  // Remove the third stream.
  pc_->RemoveStream(pc_->local_streams()->at(2));
  EXPECT_EQ(2u, pc_->local_streams()->count());

  // Remove the second stream.
  pc_->RemoveStream(pc_->local_streams()->at(1));
  EXPECT_EQ(1u, pc_->local_streams()->count());

  // Remove the first stream.
  pc_->RemoveStream(pc_->local_streams()->at(0));
  EXPECT_EQ(0u, pc_->local_streams()->count());
}

// Test that the created offer includes streams we added.
TEST_F(PeerConnectionInterfaceTest, AddedStreamsPresentInOffer) {
  CreatePeerConnection();
  AddAudioVideoStream(kStreamLabel1, "audio_track", "video_track");
  scoped_ptr<SessionDescriptionInterface> offer;
  ASSERT_TRUE(DoCreateOffer(offer.accept(), nullptr));

  const cricket::ContentInfo* audio_content =
      cricket::GetFirstAudioContent(offer->description());
  const cricket::AudioContentDescription* audio_desc =
      static_cast<const cricket::AudioContentDescription*>(
          audio_content->description);
  EXPECT_TRUE(
      ContainsTrack(audio_desc->streams(), kStreamLabel1, "audio_track"));

  const cricket::ContentInfo* video_content =
      cricket::GetFirstVideoContent(offer->description());
  const cricket::VideoContentDescription* video_desc =
      static_cast<const cricket::VideoContentDescription*>(
          video_content->description);
  EXPECT_TRUE(
      ContainsTrack(video_desc->streams(), kStreamLabel1, "video_track"));

  // Add another stream and ensure the offer includes both the old and new
  // streams.
  AddAudioVideoStream(kStreamLabel2, "audio_track2", "video_track2");
  ASSERT_TRUE(DoCreateOffer(offer.accept(), nullptr));

  audio_content = cricket::GetFirstAudioContent(offer->description());
  audio_desc = static_cast<const cricket::AudioContentDescription*>(
      audio_content->description);
  EXPECT_TRUE(
      ContainsTrack(audio_desc->streams(), kStreamLabel1, "audio_track"));
  EXPECT_TRUE(
      ContainsTrack(audio_desc->streams(), kStreamLabel2, "audio_track2"));

  video_content = cricket::GetFirstVideoContent(offer->description());
  video_desc = static_cast<const cricket::VideoContentDescription*>(
      video_content->description);
  EXPECT_TRUE(
      ContainsTrack(video_desc->streams(), kStreamLabel1, "video_track"));
  EXPECT_TRUE(
      ContainsTrack(video_desc->streams(), kStreamLabel2, "video_track2"));
}

TEST_F(PeerConnectionInterfaceTest, RemoveStream) {
  CreatePeerConnection();
  AddVideoStream(kStreamLabel1);
  ASSERT_EQ(1u, pc_->local_streams()->count());
  pc_->RemoveStream(pc_->local_streams()->at(0));
  EXPECT_EQ(0u, pc_->local_streams()->count());
}

TEST_F(PeerConnectionInterfaceTest, CreateOfferReceiveAnswer) {
  InitiateCall();
  WaitAndVerifyOnAddStream(kStreamLabel1);
  VerifyRemoteRtpHeaderExtensions();
}

TEST_F(PeerConnectionInterfaceTest, CreateOfferReceivePrAnswerAndAnswer) {
  CreatePeerConnection();
  AddVideoStream(kStreamLabel1);
  CreateOfferAsLocalDescription();
  std::string offer;
  EXPECT_TRUE(pc_->local_description()->ToString(&offer));
  CreatePrAnswerAndAnswerAsRemoteDescription(offer);
  WaitAndVerifyOnAddStream(kStreamLabel1);
}

TEST_F(PeerConnectionInterfaceTest, ReceiveOfferCreateAnswer) {
  CreatePeerConnection();
  AddVideoStream(kStreamLabel1);

  CreateOfferAsRemoteDescription();
  CreateAnswerAsLocalDescription();

  WaitAndVerifyOnAddStream(kStreamLabel1);
}

TEST_F(PeerConnectionInterfaceTest, ReceiveOfferCreatePrAnswerAndAnswer) {
  CreatePeerConnection();
  AddVideoStream(kStreamLabel1);

  CreateOfferAsRemoteDescription();
  CreatePrAnswerAsLocalDescription();
  CreateAnswerAsLocalDescription();

  WaitAndVerifyOnAddStream(kStreamLabel1);
}

TEST_F(PeerConnectionInterfaceTest, Renegotiate) {
  InitiateCall();
  ASSERT_EQ(1u, pc_->remote_streams()->count());
  pc_->RemoveStream(pc_->local_streams()->at(0));
  CreateOfferReceiveAnswer();
  EXPECT_EQ(0u, pc_->remote_streams()->count());
  AddVideoStream(kStreamLabel1);
  CreateOfferReceiveAnswer();
}

// Tests that after negotiating an audio only call, the respondent can perform a
// renegotiation that removes the audio stream.
TEST_F(PeerConnectionInterfaceTest, RenegotiateAudioOnly) {
  CreatePeerConnection();
  AddVoiceStream(kStreamLabel1);
  CreateOfferAsRemoteDescription();
  CreateAnswerAsLocalDescription();

  ASSERT_EQ(1u, pc_->remote_streams()->count());
  pc_->RemoveStream(pc_->local_streams()->at(0));
  CreateOfferReceiveAnswer();
  EXPECT_EQ(0u, pc_->remote_streams()->count());
}

// Test that candidates are generated and that we can parse our own candidates.
TEST_F(PeerConnectionInterfaceTest, IceCandidates) {
  CreatePeerConnection();

  EXPECT_FALSE(pc_->AddIceCandidate(observer_.last_candidate_.get()));
  // SetRemoteDescription takes ownership of offer.
  SessionDescriptionInterface* offer = NULL;
  AddVideoStream(kStreamLabel1);
  EXPECT_TRUE(DoCreateOffer(&offer, nullptr));
  EXPECT_TRUE(DoSetRemoteDescription(offer));

  // SetLocalDescription takes ownership of answer.
  SessionDescriptionInterface* answer = NULL;
  EXPECT_TRUE(DoCreateAnswer(&answer, nullptr));
  EXPECT_TRUE(DoSetLocalDescription(answer));

  EXPECT_TRUE_WAIT(observer_.last_candidate_.get() != NULL, kTimeout);
  EXPECT_TRUE_WAIT(observer_.ice_complete_, kTimeout);

  EXPECT_TRUE(pc_->AddIceCandidate(observer_.last_candidate_.get()));
}

// Test that CreateOffer and CreateAnswer will fail if the track labels are
// not unique.
TEST_F(PeerConnectionInterfaceTest, CreateOfferAnswerWithInvalidStream) {
  CreatePeerConnection();
  // Create a regular offer for the CreateAnswer test later.
  SessionDescriptionInterface* offer = NULL;
  EXPECT_TRUE(DoCreateOffer(&offer, nullptr));
  EXPECT_TRUE(offer != NULL);
  delete offer;
  offer = NULL;

  // Create a local stream with audio&video tracks having same label.
  AddAudioVideoStream(kStreamLabel1, "track_label", "track_label");

  // Test CreateOffer
  EXPECT_FALSE(DoCreateOffer(&offer, nullptr));

  // Test CreateAnswer
  SessionDescriptionInterface* answer = NULL;
  EXPECT_FALSE(DoCreateAnswer(&answer, nullptr));
}

// Test that we will get different SSRCs for each tracks in the offer and answer
// we created.
TEST_F(PeerConnectionInterfaceTest, SsrcInOfferAnswer) {
  CreatePeerConnection();
  // Create a local stream with audio&video tracks having different labels.
  AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");

  // Test CreateOffer
  scoped_ptr<SessionDescriptionInterface> offer;
  ASSERT_TRUE(DoCreateOffer(offer.use(), nullptr));
  int audio_ssrc = 0;
  int video_ssrc = 0;
  EXPECT_TRUE(GetFirstSsrc(GetFirstAudioContent(offer->description()),
                           &audio_ssrc));
  EXPECT_TRUE(GetFirstSsrc(GetFirstVideoContent(offer->description()),
                           &video_ssrc));
  EXPECT_NE(audio_ssrc, video_ssrc);

  // Test CreateAnswer
  EXPECT_TRUE(DoSetRemoteDescription(offer.release()));
  scoped_ptr<SessionDescriptionInterface> answer;
  ASSERT_TRUE(DoCreateAnswer(answer.use(), nullptr));
  audio_ssrc = 0;
  video_ssrc = 0;
  EXPECT_TRUE(GetFirstSsrc(GetFirstAudioContent(answer->description()),
                           &audio_ssrc));
  EXPECT_TRUE(GetFirstSsrc(GetFirstVideoContent(answer->description()),
                           &video_ssrc));
  EXPECT_NE(audio_ssrc, video_ssrc);
}

// Test that we can specify a certain track that we want statistics about.
TEST_F(PeerConnectionInterfaceTest, GetStatsForSpecificTrack) {
  InitiateCall();
  ASSERT_LT(0u, pc_->remote_streams()->count());
  ASSERT_LT(0u, pc_->remote_streams()->at(0)->GetAudioTracks().size());
  scoped_refptr<MediaStreamTrackInterface> remote_audio =
      pc_->remote_streams()->at(0)->GetAudioTracks()[0];
  EXPECT_TRUE(DoGetStats(remote_audio));

  // Remove the stream. Since we are sending to our selves the local
  // and the remote stream is the same.
  pc_->RemoveStream(pc_->local_streams()->at(0));
  // Do a re-negotiation.
  CreateOfferReceiveAnswer();

  ASSERT_EQ(0u, pc_->remote_streams()->count());

  // Test that we still can get statistics for the old track. Even if it is not
  // sent any longer.
  EXPECT_TRUE(DoGetStats(remote_audio));
}

// Test that we can get stats on a video track.
TEST_F(PeerConnectionInterfaceTest, GetStatsForVideoTrack) {
  InitiateCall();
  ASSERT_LT(0u, pc_->remote_streams()->count());
  ASSERT_LT(0u, pc_->remote_streams()->at(0)->GetVideoTracks().size());
  scoped_refptr<MediaStreamTrackInterface> remote_video =
      pc_->remote_streams()->at(0)->GetVideoTracks()[0];
  EXPECT_TRUE(DoGetStats(remote_video));
}

// Test that we don't get statistics for an invalid track.
// TODO(tommi): Fix this test.  DoGetStats will return true
// for the unknown track (since GetStats is async), but no
// data is returned for the track.
TEST_F(PeerConnectionInterfaceTest, DISABLED_GetStatsForInvalidTrack) {
  InitiateCall();
  scoped_refptr<AudioTrackInterface> unknown_audio_track(
      pc_factory_->CreateAudioTrack("unknown track", NULL));
  EXPECT_FALSE(DoGetStats(unknown_audio_track));
}

// This test setup two RTP data channels in loop back.
TEST_F(PeerConnectionInterfaceTest, TestDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);
  scoped_refptr<DataChannelInterface> data1  =
      pc_->CreateDataChannel("test1", NULL);
  scoped_refptr<DataChannelInterface> data2  =
      pc_->CreateDataChannel("test2", NULL);
  ASSERT_TRUE(data1 != NULL);
  rtc::scoped_ptr<MockDataChannelObserver> observer1(
      new MockDataChannelObserver(data1));
  rtc::scoped_ptr<MockDataChannelObserver> observer2(
      new MockDataChannelObserver(data2));

  EXPECT_EQ(DataChannelInterface::kConnecting, data1->state());
  EXPECT_EQ(DataChannelInterface::kConnecting, data2->state());
  std::string data_to_send1 = "testing testing";
  std::string data_to_send2 = "testing something else";
  EXPECT_FALSE(data1->Send(DataBuffer(data_to_send1)));

  CreateOfferReceiveAnswer();
  EXPECT_TRUE_WAIT(observer1->IsOpen(), kTimeout);
  EXPECT_TRUE_WAIT(observer2->IsOpen(), kTimeout);

  EXPECT_EQ(DataChannelInterface::kOpen, data1->state());
  EXPECT_EQ(DataChannelInterface::kOpen, data2->state());
  EXPECT_TRUE(data1->Send(DataBuffer(data_to_send1)));
  EXPECT_TRUE(data2->Send(DataBuffer(data_to_send2)));

  EXPECT_EQ_WAIT(data_to_send1, observer1->last_message(), kTimeout);
  EXPECT_EQ_WAIT(data_to_send2, observer2->last_message(), kTimeout);

  data1->Close();
  EXPECT_EQ(DataChannelInterface::kClosing, data1->state());
  CreateOfferReceiveAnswer();
  EXPECT_FALSE(observer1->IsOpen());
  EXPECT_EQ(DataChannelInterface::kClosed, data1->state());
  EXPECT_TRUE(observer2->IsOpen());

  data_to_send2 = "testing something else again";
  EXPECT_TRUE(data2->Send(DataBuffer(data_to_send2)));

  EXPECT_EQ_WAIT(data_to_send2, observer2->last_message(), kTimeout);
}

// This test verifies that sendnig binary data over RTP data channels should
// fail.
TEST_F(PeerConnectionInterfaceTest, TestSendBinaryOnRtpDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);
  scoped_refptr<DataChannelInterface> data1  =
      pc_->CreateDataChannel("test1", NULL);
  scoped_refptr<DataChannelInterface> data2  =
      pc_->CreateDataChannel("test2", NULL);
  ASSERT_TRUE(data1 != NULL);
  rtc::scoped_ptr<MockDataChannelObserver> observer1(
      new MockDataChannelObserver(data1));
  rtc::scoped_ptr<MockDataChannelObserver> observer2(
      new MockDataChannelObserver(data2));

  EXPECT_EQ(DataChannelInterface::kConnecting, data1->state());
  EXPECT_EQ(DataChannelInterface::kConnecting, data2->state());

  CreateOfferReceiveAnswer();
  EXPECT_TRUE_WAIT(observer1->IsOpen(), kTimeout);
  EXPECT_TRUE_WAIT(observer2->IsOpen(), kTimeout);

  EXPECT_EQ(DataChannelInterface::kOpen, data1->state());
  EXPECT_EQ(DataChannelInterface::kOpen, data2->state());

  rtc::Buffer buffer("test", 4);
  EXPECT_FALSE(data1->Send(DataBuffer(buffer, true)));
}

// This test setup a RTP data channels in loop back and test that a channel is
// opened even if the remote end answer with a zero SSRC.
TEST_F(PeerConnectionInterfaceTest, TestSendOnlyDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);
  scoped_refptr<DataChannelInterface> data1  =
      pc_->CreateDataChannel("test1", NULL);
  rtc::scoped_ptr<MockDataChannelObserver> observer1(
      new MockDataChannelObserver(data1));

  CreateOfferReceiveAnswerWithoutSsrc();

  EXPECT_TRUE_WAIT(observer1->IsOpen(), kTimeout);

  data1->Close();
  EXPECT_EQ(DataChannelInterface::kClosing, data1->state());
  CreateOfferReceiveAnswerWithoutSsrc();
  EXPECT_EQ(DataChannelInterface::kClosed, data1->state());
  EXPECT_FALSE(observer1->IsOpen());
}

// This test that if a data channel is added in an answer a receive only channel
// channel is created.
TEST_F(PeerConnectionInterfaceTest, TestReceiveOnlyDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  std::string offer_label = "offer_channel";
  scoped_refptr<DataChannelInterface> offer_channel  =
      pc_->CreateDataChannel(offer_label, NULL);

  CreateOfferAsLocalDescription();

  // Replace the data channel label in the offer and apply it as an answer.
  std::string receive_label = "answer_channel";
  std::string sdp;
  EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
  rtc::replace_substrs(offer_label.c_str(), offer_label.length(),
                             receive_label.c_str(), receive_label.length(),
                             &sdp);
  CreateAnswerAsRemoteDescription(sdp);

  // Verify that a new incoming data channel has been created and that
  // it is open but can't we written to.
  ASSERT_TRUE(observer_.last_datachannel_ != NULL);
  DataChannelInterface* received_channel = observer_.last_datachannel_;
  EXPECT_EQ(DataChannelInterface::kConnecting, received_channel->state());
  EXPECT_EQ(receive_label, received_channel->label());
  EXPECT_FALSE(received_channel->Send(DataBuffer("something")));

  // Verify that the channel we initially offered has been rejected.
  EXPECT_EQ(DataChannelInterface::kClosed, offer_channel->state());

  // Do another offer / answer exchange and verify that the data channel is
  // opened.
  CreateOfferReceiveAnswer();
  EXPECT_EQ_WAIT(DataChannelInterface::kOpen, received_channel->state(),
                 kTimeout);
}

// This test that no data channel is returned if a reliable channel is
// requested.
// TODO(perkj): Remove this test once reliable channels are implemented.
TEST_F(PeerConnectionInterfaceTest, CreateReliableRtpDataChannelShouldFail) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  std::string label = "test";
  webrtc::DataChannelInit config;
  config.reliable = true;
  scoped_refptr<DataChannelInterface> channel  =
      pc_->CreateDataChannel(label, &config);
  EXPECT_TRUE(channel == NULL);
}

// Verifies that duplicated label is not allowed for RTP data channel.
TEST_F(PeerConnectionInterfaceTest, RtpDuplicatedLabelNotAllowed) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  std::string label = "test";
  scoped_refptr<DataChannelInterface> channel =
      pc_->CreateDataChannel(label, nullptr);
  EXPECT_NE(channel, nullptr);

  scoped_refptr<DataChannelInterface> dup_channel =
      pc_->CreateDataChannel(label, nullptr);
  EXPECT_EQ(dup_channel, nullptr);
}

// This tests that a SCTP data channel is returned using different
// DataChannelInit configurations.
TEST_F(PeerConnectionInterfaceTest, CreateSctpDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowDtlsSctpDataChannels();
  CreatePeerConnection(&constraints);

  webrtc::DataChannelInit config;

  scoped_refptr<DataChannelInterface> channel =
      pc_->CreateDataChannel("1", &config);
  EXPECT_TRUE(channel != NULL);
  EXPECT_TRUE(channel->reliable());
  EXPECT_TRUE(observer_.renegotiation_needed_);
  observer_.renegotiation_needed_ = false;

  config.ordered = false;
  channel = pc_->CreateDataChannel("2", &config);
  EXPECT_TRUE(channel != NULL);
  EXPECT_TRUE(channel->reliable());
  EXPECT_FALSE(observer_.renegotiation_needed_);

  config.ordered = true;
  config.maxRetransmits = 0;
  channel = pc_->CreateDataChannel("3", &config);
  EXPECT_TRUE(channel != NULL);
  EXPECT_FALSE(channel->reliable());
  EXPECT_FALSE(observer_.renegotiation_needed_);

  config.maxRetransmits = -1;
  config.maxRetransmitTime = 0;
  channel = pc_->CreateDataChannel("4", &config);
  EXPECT_TRUE(channel != NULL);
  EXPECT_FALSE(channel->reliable());
  EXPECT_FALSE(observer_.renegotiation_needed_);
}

// This tests that no data channel is returned if both maxRetransmits and
// maxRetransmitTime are set for SCTP data channels.
TEST_F(PeerConnectionInterfaceTest,
       CreateSctpDataChannelShouldFailForInvalidConfig) {
  FakeConstraints constraints;
  constraints.SetAllowDtlsSctpDataChannels();
  CreatePeerConnection(&constraints);

  std::string label = "test";
  webrtc::DataChannelInit config;
  config.maxRetransmits = 0;
  config.maxRetransmitTime = 0;

  scoped_refptr<DataChannelInterface> channel =
      pc_->CreateDataChannel(label, &config);
  EXPECT_TRUE(channel == NULL);
}

// The test verifies that creating a SCTP data channel with an id already in use
// or out of range should fail.
TEST_F(PeerConnectionInterfaceTest,
       CreateSctpDataChannelWithInvalidIdShouldFail) {
  FakeConstraints constraints;
  constraints.SetAllowDtlsSctpDataChannels();
  CreatePeerConnection(&constraints);

  webrtc::DataChannelInit config;
  scoped_refptr<DataChannelInterface> channel;

  config.id = 1;
  channel = pc_->CreateDataChannel("1", &config);
  EXPECT_TRUE(channel != NULL);
  EXPECT_EQ(1, channel->id());

  channel = pc_->CreateDataChannel("x", &config);
  EXPECT_TRUE(channel == NULL);

  config.id = cricket::kMaxSctpSid;
  channel = pc_->CreateDataChannel("max", &config);
  EXPECT_TRUE(channel != NULL);
  EXPECT_EQ(config.id, channel->id());

  config.id = cricket::kMaxSctpSid + 1;
  channel = pc_->CreateDataChannel("x", &config);
  EXPECT_TRUE(channel == NULL);
}

// Verifies that duplicated label is allowed for SCTP data channel.
TEST_F(PeerConnectionInterfaceTest, SctpDuplicatedLabelAllowed) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);

  std::string label = "test";
  scoped_refptr<DataChannelInterface> channel =
      pc_->CreateDataChannel(label, nullptr);
  EXPECT_NE(channel, nullptr);

  scoped_refptr<DataChannelInterface> dup_channel =
      pc_->CreateDataChannel(label, nullptr);
  EXPECT_NE(dup_channel, nullptr);
}

// This test verifies that OnRenegotiationNeeded is fired for every new RTP
// DataChannel.
TEST_F(PeerConnectionInterfaceTest, RenegotiationNeededForNewRtpDataChannel) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  scoped_refptr<DataChannelInterface> dc1  =
      pc_->CreateDataChannel("test1", NULL);
  EXPECT_TRUE(observer_.renegotiation_needed_);
  observer_.renegotiation_needed_ = false;

  scoped_refptr<DataChannelInterface> dc2  =
      pc_->CreateDataChannel("test2", NULL);
  EXPECT_TRUE(observer_.renegotiation_needed_);
}

// This test that a data channel closes when a PeerConnection is deleted/closed.
TEST_F(PeerConnectionInterfaceTest, DataChannelCloseWhenPeerConnectionClose) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  scoped_refptr<DataChannelInterface> data1  =
      pc_->CreateDataChannel("test1", NULL);
  scoped_refptr<DataChannelInterface> data2  =
      pc_->CreateDataChannel("test2", NULL);
  ASSERT_TRUE(data1 != NULL);
  rtc::scoped_ptr<MockDataChannelObserver> observer1(
      new MockDataChannelObserver(data1));
  rtc::scoped_ptr<MockDataChannelObserver> observer2(
      new MockDataChannelObserver(data2));

  CreateOfferReceiveAnswer();
  EXPECT_TRUE_WAIT(observer1->IsOpen(), kTimeout);
  EXPECT_TRUE_WAIT(observer2->IsOpen(), kTimeout);

  ReleasePeerConnection();
  EXPECT_EQ(DataChannelInterface::kClosed, data1->state());
  EXPECT_EQ(DataChannelInterface::kClosed, data2->state());
}

// This test that data channels can be rejected in an answer.
TEST_F(PeerConnectionInterfaceTest, TestRejectDataChannelInAnswer) {
  FakeConstraints constraints;
  constraints.SetAllowRtpDataChannels();
  CreatePeerConnection(&constraints);

  scoped_refptr<DataChannelInterface> offer_channel(
      pc_->CreateDataChannel("offer_channel", NULL));

  CreateOfferAsLocalDescription();

  // Create an answer where the m-line for data channels are rejected.
  std::string sdp;
  EXPECT_TRUE(pc_->local_description()->ToString(&sdp));
  webrtc::JsepSessionDescription* answer = new webrtc::JsepSessionDescription(
      SessionDescriptionInterface::kAnswer);
  EXPECT_TRUE(answer->Initialize(sdp, NULL));
  cricket::ContentInfo* data_info =
      answer->description()->GetContentByName("data");
  data_info->rejected = true;

  DoSetRemoteDescription(answer);
  EXPECT_EQ(DataChannelInterface::kClosed, offer_channel->state());
}

// Test that we can create a session description from an SDP string from
// FireFox, use it as a remote session description, generate an answer and use
// the answer as a local description.
TEST_F(PeerConnectionInterfaceTest, ReceiveFireFoxOffer) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");
  SessionDescriptionInterface* desc =
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       webrtc::kFireFoxSdpOffer, nullptr);
  EXPECT_TRUE(DoSetSessionDescription(desc, false));
  CreateAnswerAsLocalDescription();
  ASSERT_TRUE(pc_->local_description() != NULL);
  ASSERT_TRUE(pc_->remote_description() != NULL);

  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(pc_->local_description()->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  content =
      cricket::GetFirstVideoContent(pc_->local_description()->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
#ifdef HAVE_SCTP
  content =
      cricket::GetFirstDataContent(pc_->local_description()->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_TRUE(content->rejected);
#endif
}

// Test that we can create an audio only offer and receive an answer with a
// limited set of audio codecs and receive an updated offer with more audio
// codecs, where the added codecs are not supported.
TEST_F(PeerConnectionInterfaceTest, ReceiveUpdatedAudioOfferWithBadCodecs) {
  CreatePeerConnection();
  AddVoiceStream("audio_label");
  CreateOfferAsLocalDescription();

  SessionDescriptionInterface* answer =
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kAnswer,
                                       webrtc::kAudioSdp, nullptr);
  EXPECT_TRUE(DoSetSessionDescription(answer, false));

  SessionDescriptionInterface* updated_offer =
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       webrtc::kAudioSdpWithUnsupportedCodecs,
                                       nullptr);
  EXPECT_TRUE(DoSetSessionDescription(updated_offer, false));
  CreateAnswerAsLocalDescription();
}

// Test that if we're receiving (but not sending) a track, subsequent offers
// will have m-lines with a=recvonly.
TEST_F(PeerConnectionInterfaceTest, CreateSubsequentRecvOnlyOffer) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithStream1);
  CreateAnswerAsLocalDescription();

  // At this point we should be receiving stream 1, but not sending anything.
  // A new offer should be recvonly.
  SessionDescriptionInterface* offer;
  DoCreateOffer(&offer, nullptr);

  const cricket::ContentInfo* video_content =
      cricket::GetFirstVideoContent(offer->description());
  const cricket::VideoContentDescription* video_desc =
      static_cast<const cricket::VideoContentDescription*>(
          video_content->description);
  ASSERT_EQ(cricket::MD_RECVONLY, video_desc->direction());

  const cricket::ContentInfo* audio_content =
      cricket::GetFirstAudioContent(offer->description());
  const cricket::AudioContentDescription* audio_desc =
      static_cast<const cricket::AudioContentDescription*>(
          audio_content->description);
  ASSERT_EQ(cricket::MD_RECVONLY, audio_desc->direction());
}

// Test that if we're receiving (but not sending) a track, and the
// offerToReceiveVideo/offerToReceiveAudio constraints are explicitly set to
// false, the generated m-lines will be a=inactive.
TEST_F(PeerConnectionInterfaceTest, CreateSubsequentInactiveOffer) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithStream1);
  CreateAnswerAsLocalDescription();

  // At this point we should be receiving stream 1, but not sending anything.
  // A new offer would be recvonly, but we'll set the "no receive" constraints
  // to make it inactive.
  SessionDescriptionInterface* offer;
  FakeConstraints offer_constraints;
  offer_constraints.AddMandatory(
      webrtc::MediaConstraintsInterface::kOfferToReceiveVideo, false);
  offer_constraints.AddMandatory(
      webrtc::MediaConstraintsInterface::kOfferToReceiveAudio, false);
  DoCreateOffer(&offer, &offer_constraints);

  const cricket::ContentInfo* video_content =
      cricket::GetFirstVideoContent(offer->description());
  const cricket::VideoContentDescription* video_desc =
      static_cast<const cricket::VideoContentDescription*>(
          video_content->description);
  ASSERT_EQ(cricket::MD_INACTIVE, video_desc->direction());

  const cricket::ContentInfo* audio_content =
      cricket::GetFirstAudioContent(offer->description());
  const cricket::AudioContentDescription* audio_desc =
      static_cast<const cricket::AudioContentDescription*>(
          audio_content->description);
  ASSERT_EQ(cricket::MD_INACTIVE, audio_desc->direction());
}

// Test that we can use SetConfiguration to change the ICE servers of the
// PortAllocator.
TEST_F(PeerConnectionInterfaceTest, SetConfigurationChangesIceServers) {
  CreatePeerConnection();

  PeerConnectionInterface::RTCConfiguration config;
  PeerConnectionInterface::IceServer server;
  server.uri = "stun:test_hostname";
  config.servers.push_back(server);
  EXPECT_TRUE(pc_->SetConfiguration(config));

  cricket::FakePortAllocator* allocator =
      port_allocator_factory_->last_created_allocator();
  EXPECT_EQ(1u, allocator->stun_servers().size());
  EXPECT_EQ("test_hostname", allocator->stun_servers().begin()->hostname());
}

// Test that PeerConnection::Close changes the states to closed and all remote
// tracks change state to ended.
TEST_F(PeerConnectionInterfaceTest, CloseAndTestStreamsAndStates) {
  // Initialize a PeerConnection and negotiate local and remote session
  // description.
  InitiateCall();
  ASSERT_EQ(1u, pc_->local_streams()->count());
  ASSERT_EQ(1u, pc_->remote_streams()->count());

  pc_->Close();

  EXPECT_EQ(PeerConnectionInterface::kClosed, pc_->signaling_state());
  EXPECT_EQ(PeerConnectionInterface::kIceConnectionClosed,
            pc_->ice_connection_state());
  EXPECT_EQ(PeerConnectionInterface::kIceGatheringComplete,
            pc_->ice_gathering_state());

  EXPECT_EQ(1u, pc_->local_streams()->count());
  EXPECT_EQ(1u, pc_->remote_streams()->count());

  scoped_refptr<MediaStreamInterface> remote_stream =
          pc_->remote_streams()->at(0);
  EXPECT_EQ(MediaStreamTrackInterface::kEnded,
            remote_stream->GetVideoTracks()[0]->state());
  EXPECT_EQ(MediaStreamTrackInterface::kEnded,
            remote_stream->GetAudioTracks()[0]->state());
}

// Test that PeerConnection methods fails gracefully after
// PeerConnection::Close has been called.
TEST_F(PeerConnectionInterfaceTest, CloseAndTestMethods) {
  CreatePeerConnection();
  AddAudioVideoStream(kStreamLabel1, "audio_label", "video_label");
  CreateOfferAsRemoteDescription();
  CreateAnswerAsLocalDescription();

  ASSERT_EQ(1u, pc_->local_streams()->count());
  scoped_refptr<MediaStreamInterface> local_stream =
      pc_->local_streams()->at(0);

  pc_->Close();

  pc_->RemoveStream(local_stream);
  EXPECT_FALSE(pc_->AddStream(local_stream));

  ASSERT_FALSE(local_stream->GetAudioTracks().empty());
  rtc::scoped_refptr<webrtc::DtmfSenderInterface> dtmf_sender(
      pc_->CreateDtmfSender(local_stream->GetAudioTracks()[0]));
  EXPECT_TRUE(NULL == dtmf_sender);  // local stream has been removed.

  EXPECT_TRUE(pc_->CreateDataChannel("test", NULL) == NULL);

  EXPECT_TRUE(pc_->local_description() != NULL);
  EXPECT_TRUE(pc_->remote_description() != NULL);

  rtc::scoped_ptr<SessionDescriptionInterface> offer;
  EXPECT_TRUE(DoCreateOffer(offer.use(), nullptr));
  rtc::scoped_ptr<SessionDescriptionInterface> answer;
  EXPECT_TRUE(DoCreateAnswer(answer.use(), nullptr));

  std::string sdp;
  ASSERT_TRUE(pc_->remote_description()->ToString(&sdp));
  SessionDescriptionInterface* remote_offer =
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                       sdp, NULL);
  EXPECT_FALSE(DoSetRemoteDescription(remote_offer));

  ASSERT_TRUE(pc_->local_description()->ToString(&sdp));
  SessionDescriptionInterface* local_offer =
        webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer,
                                         sdp, NULL);
  EXPECT_FALSE(DoSetLocalDescription(local_offer));
}

// Test that GetStats can still be called after PeerConnection::Close.
TEST_F(PeerConnectionInterfaceTest, CloseAndGetStats) {
  InitiateCall();
  pc_->Close();
  DoGetStats(NULL);
}

// NOTE: The series of tests below come from what used to be
// mediastreamsignaling_unittest.cc, and are mostly aimed at testing that
// setting a remote or local description has the expected effects.

// This test verifies that the remote MediaStreams corresponding to a received
// SDP string is created. In this test the two separate MediaStreams are
// signaled.
TEST_F(PeerConnectionInterfaceTest, UpdateRemoteStreams) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithStream1);

  rtc::scoped_refptr<StreamCollection> reference(CreateStreamCollection(1));
  EXPECT_TRUE(
      CompareStreamCollections(observer_.remote_streams(), reference.get()));
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);
  EXPECT_TRUE(remote_stream->GetVideoTracks()[0]->GetSource() != nullptr);

  // Create a session description based on another SDP with another
  // MediaStream.
  CreateAndSetRemoteOffer(kSdpStringWithStream1And2);

  rtc::scoped_refptr<StreamCollection> reference2(CreateStreamCollection(2));
  EXPECT_TRUE(
      CompareStreamCollections(observer_.remote_streams(), reference2.get()));
}

// This test verifies that when remote tracks are added/removed from SDP, the
// created remote streams are updated appropriately.
TEST_F(PeerConnectionInterfaceTest,
       AddRemoveTrackFromExistingRemoteMediaStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  rtc::scoped_ptr<SessionDescriptionInterface> desc_ms1;
  CreateSessionDescriptionAndReference(1, 1, desc_ms1.accept());
  EXPECT_TRUE(DoSetRemoteDescription(desc_ms1.release()));
  EXPECT_TRUE(CompareStreamCollections(observer_.remote_streams(),
                                       reference_collection_));

  // Add extra audio and video tracks to the same MediaStream.
  rtc::scoped_ptr<SessionDescriptionInterface> desc_ms1_two_tracks;
  CreateSessionDescriptionAndReference(2, 2, desc_ms1_two_tracks.accept());
  EXPECT_TRUE(DoSetRemoteDescription(desc_ms1_two_tracks.release()));
  EXPECT_TRUE(CompareStreamCollections(observer_.remote_streams(),
                                       reference_collection_));

  // Remove the extra audio and video tracks.
  rtc::scoped_ptr<SessionDescriptionInterface> desc_ms2;
  CreateSessionDescriptionAndReference(1, 1, desc_ms2.accept());
  EXPECT_TRUE(DoSetRemoteDescription(desc_ms2.release()));
  EXPECT_TRUE(CompareStreamCollections(observer_.remote_streams(),
                                       reference_collection_));
}

// This tests that remote tracks are ended if a local session description is set
// that rejects the media content type.
TEST_F(PeerConnectionInterfaceTest, RejectMediaContent) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  // First create and set a remote offer, then reject its video content in our
  // answer.
  CreateAndSetRemoteOffer(kSdpStringWithStream1);
  ASSERT_EQ(1u, observer_.remote_streams()->count());
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);
  ASSERT_EQ(1u, remote_stream->GetVideoTracks().size());
  ASSERT_EQ(1u, remote_stream->GetAudioTracks().size());

  rtc::scoped_refptr<webrtc::VideoTrackInterface> remote_video =
      remote_stream->GetVideoTracks()[0];
  EXPECT_EQ(webrtc::MediaStreamTrackInterface::kLive, remote_video->state());
  rtc::scoped_refptr<webrtc::AudioTrackInterface> remote_audio =
      remote_stream->GetAudioTracks()[0];
  EXPECT_EQ(webrtc::MediaStreamTrackInterface::kLive, remote_audio->state());

  rtc::scoped_ptr<SessionDescriptionInterface> local_answer;
  EXPECT_TRUE(DoCreateAnswer(local_answer.accept(), nullptr));
  cricket::ContentInfo* video_info =
      local_answer->description()->GetContentByName("video");
  video_info->rejected = true;
  EXPECT_TRUE(DoSetLocalDescription(local_answer.release()));
  EXPECT_EQ(webrtc::MediaStreamTrackInterface::kEnded, remote_video->state());
  EXPECT_EQ(webrtc::MediaStreamTrackInterface::kLive, remote_audio->state());

  // Now create an offer where we reject both video and audio.
  rtc::scoped_ptr<SessionDescriptionInterface> local_offer;
  EXPECT_TRUE(DoCreateOffer(local_offer.accept(), nullptr));
  video_info = local_offer->description()->GetContentByName("video");
  ASSERT_TRUE(video_info != nullptr);
  video_info->rejected = true;
  cricket::ContentInfo* audio_info =
      local_offer->description()->GetContentByName("audio");
  ASSERT_TRUE(audio_info != nullptr);
  audio_info->rejected = true;
  EXPECT_TRUE(DoSetLocalDescription(local_offer.release()));
  EXPECT_EQ(webrtc::MediaStreamTrackInterface::kEnded, remote_video->state());
  EXPECT_EQ(webrtc::MediaStreamTrackInterface::kEnded, remote_audio->state());
}

// This tests that we won't crash if the remote track has been removed outside
// of PeerConnection and then PeerConnection tries to reject the track.
TEST_F(PeerConnectionInterfaceTest, RemoveTrackThenRejectMediaContent) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithStream1);
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);
  remote_stream->RemoveTrack(remote_stream->GetVideoTracks()[0]);
  remote_stream->RemoveTrack(remote_stream->GetAudioTracks()[0]);

  rtc::scoped_ptr<SessionDescriptionInterface> local_answer(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kAnswer,
                                       kSdpStringWithStream1, nullptr));
  cricket::ContentInfo* video_info =
      local_answer->description()->GetContentByName("video");
  video_info->rejected = true;
  cricket::ContentInfo* audio_info =
      local_answer->description()->GetContentByName("audio");
  audio_info->rejected = true;
  EXPECT_TRUE(DoSetLocalDescription(local_answer.release()));

  // No crash is a pass.
}

// This tests that if a recvonly remote description is set, no remote streams
// will be created, even if the description contains SSRCs/MSIDs.
// See: https://code.google.com/p/webrtc/issues/detail?id=5054
TEST_F(PeerConnectionInterfaceTest, RecvonlyDescriptionDoesntCreateStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);

  std::string recvonly_offer = kSdpStringWithStream1;
  rtc::replace_substrs(kSendrecv, strlen(kSendrecv), kRecvonly,
                       strlen(kRecvonly), &recvonly_offer);
  CreateAndSetRemoteOffer(recvonly_offer);

  EXPECT_EQ(0u, observer_.remote_streams()->count());
}

// This tests that a default MediaStream is created if a remote session
// description doesn't contain any streams and no MSID support.
// It also tests that the default stream is updated if a video m-line is added
// in a subsequent session description.
TEST_F(PeerConnectionInterfaceTest, SdpWithoutMsidCreatesDefaultStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithoutStreamsAudioOnly);

  ASSERT_EQ(1u, observer_.remote_streams()->count());
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);

  EXPECT_EQ(1u, remote_stream->GetAudioTracks().size());
  EXPECT_EQ(0u, remote_stream->GetVideoTracks().size());
  EXPECT_EQ("default", remote_stream->label());

  CreateAndSetRemoteOffer(kSdpStringWithoutStreams);
  ASSERT_EQ(1u, observer_.remote_streams()->count());
  ASSERT_EQ(1u, remote_stream->GetAudioTracks().size());
  EXPECT_EQ("defaulta0", remote_stream->GetAudioTracks()[0]->id());
  ASSERT_EQ(1u, remote_stream->GetVideoTracks().size());
  EXPECT_EQ("defaultv0", remote_stream->GetVideoTracks()[0]->id());
}

// This tests that a default MediaStream is created if a remote session
// description doesn't contain any streams and media direction is send only.
TEST_F(PeerConnectionInterfaceTest,
       SendOnlySdpWithoutMsidCreatesDefaultStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringSendOnlyWithoutStreams);

  ASSERT_EQ(1u, observer_.remote_streams()->count());
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);

  EXPECT_EQ(1u, remote_stream->GetAudioTracks().size());
  EXPECT_EQ(1u, remote_stream->GetVideoTracks().size());
  EXPECT_EQ("default", remote_stream->label());
}

// This tests that it won't crash when PeerConnection tries to remove
// a remote track that as already been removed from the MediaStream.
TEST_F(PeerConnectionInterfaceTest, RemoveAlreadyGoneRemoteStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithStream1);
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);
  remote_stream->RemoveTrack(remote_stream->GetAudioTracks()[0]);
  remote_stream->RemoveTrack(remote_stream->GetVideoTracks()[0]);

  CreateAndSetRemoteOffer(kSdpStringWithoutStreams);

  // No crash is a pass.
}

// This tests that a default MediaStream is created if the remote session
// description doesn't contain any streams and don't contain an indication if
// MSID is supported.
TEST_F(PeerConnectionInterfaceTest,
       SdpWithoutMsidAndStreamsCreatesDefaultStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithoutStreams);

  ASSERT_EQ(1u, observer_.remote_streams()->count());
  MediaStreamInterface* remote_stream = observer_.remote_streams()->at(0);
  EXPECT_EQ(1u, remote_stream->GetAudioTracks().size());
  EXPECT_EQ(1u, remote_stream->GetVideoTracks().size());
}

// This tests that a default MediaStream is not created if the remote session
// description doesn't contain any streams but does support MSID.
TEST_F(PeerConnectionInterfaceTest, SdpWithMsidDontCreatesDefaultStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithMsidWithoutStreams);
  EXPECT_EQ(0u, observer_.remote_streams()->count());
}

// This tests that a default MediaStream is not created if a remote session
// description is updated to not have any MediaStreams.
TEST_F(PeerConnectionInterfaceTest, VerifyDefaultStreamIsNotCreated) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  CreateAndSetRemoteOffer(kSdpStringWithStream1);
  rtc::scoped_refptr<StreamCollection> reference(CreateStreamCollection(1));
  EXPECT_TRUE(
      CompareStreamCollections(observer_.remote_streams(), reference.get()));

  CreateAndSetRemoteOffer(kSdpStringWithoutStreams);
  EXPECT_EQ(0u, observer_.remote_streams()->count());
}

// This tests that an RtpSender is created when the local description is set
// after adding a local stream.
// TODO(deadbeef): This test and the one below it need to be updated when
// an RtpSender's lifetime isn't determined by when a local description is set.
TEST_F(PeerConnectionInterfaceTest, LocalDescriptionChanged) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  // Create an offer just to ensure we have an identity before we manually
  // call SetLocalDescription.
  rtc::scoped_ptr<SessionDescriptionInterface> throwaway;
  ASSERT_TRUE(DoCreateOffer(throwaway.accept(), nullptr));

  rtc::scoped_ptr<SessionDescriptionInterface> desc_1;
  CreateSessionDescriptionAndReference(2, 2, desc_1.accept());

  pc_->AddStream(reference_collection_->at(0));
  EXPECT_TRUE(DoSetLocalDescription(desc_1.release()));
  auto senders = pc_->GetSenders();
  EXPECT_EQ(4u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[1]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[1]));

  // Remove an audio and video track.
  rtc::scoped_ptr<SessionDescriptionInterface> desc_2;
  CreateSessionDescriptionAndReference(1, 1, desc_2.accept());
  EXPECT_TRUE(DoSetLocalDescription(desc_2.release()));
  senders = pc_->GetSenders();
  EXPECT_EQ(2u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));
  EXPECT_FALSE(ContainsSender(senders, kAudioTracks[1]));
  EXPECT_FALSE(ContainsSender(senders, kVideoTracks[1]));
}

// This tests that an RtpSender is created when the local description is set
// before adding a local stream.
TEST_F(PeerConnectionInterfaceTest,
       AddLocalStreamAfterLocalDescriptionChanged) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  // Create an offer just to ensure we have an identity before we manually
  // call SetLocalDescription.
  rtc::scoped_ptr<SessionDescriptionInterface> throwaway;
  ASSERT_TRUE(DoCreateOffer(throwaway.accept(), nullptr));

  rtc::scoped_ptr<SessionDescriptionInterface> desc_1;
  CreateSessionDescriptionAndReference(2, 2, desc_1.accept());

  EXPECT_TRUE(DoSetLocalDescription(desc_1.release()));
  auto senders = pc_->GetSenders();
  EXPECT_EQ(0u, senders.size());

  pc_->AddStream(reference_collection_->at(0));
  senders = pc_->GetSenders();
  EXPECT_EQ(4u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[1]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[1]));
}

// This tests that the expected behavior occurs if the SSRC on a local track is
// changed when SetLocalDescription is called.
TEST_F(PeerConnectionInterfaceTest,
       ChangeSsrcOnTrackInLocalSessionDescription) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  // Create an offer just to ensure we have an identity before we manually
  // call SetLocalDescription.
  rtc::scoped_ptr<SessionDescriptionInterface> throwaway;
  ASSERT_TRUE(DoCreateOffer(throwaway.accept(), nullptr));

  rtc::scoped_ptr<SessionDescriptionInterface> desc;
  CreateSessionDescriptionAndReference(1, 1, desc.accept());
  std::string sdp;
  desc->ToString(&sdp);

  pc_->AddStream(reference_collection_->at(0));
  EXPECT_TRUE(DoSetLocalDescription(desc.release()));
  auto senders = pc_->GetSenders();
  EXPECT_EQ(2u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));

  // Change the ssrc of the audio and video track.
  std::string ssrc_org = "a=ssrc:1";
  std::string ssrc_to = "a=ssrc:97";
  rtc::replace_substrs(ssrc_org.c_str(), ssrc_org.length(), ssrc_to.c_str(),
                       ssrc_to.length(), &sdp);
  ssrc_org = "a=ssrc:2";
  ssrc_to = "a=ssrc:98";
  rtc::replace_substrs(ssrc_org.c_str(), ssrc_org.length(), ssrc_to.c_str(),
                       ssrc_to.length(), &sdp);
  rtc::scoped_ptr<SessionDescriptionInterface> updated_desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer, sdp,
                                       nullptr));

  EXPECT_TRUE(DoSetLocalDescription(updated_desc.release()));
  senders = pc_->GetSenders();
  EXPECT_EQ(2u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));
  // TODO(deadbeef): Once RtpSenders expose parameters, check that the SSRC
  // changed.
}

// This tests that the expected behavior occurs if a new session description is
// set with the same tracks, but on a different MediaStream.
TEST_F(PeerConnectionInterfaceTest, SignalSameTracksInSeparateMediaStream) {
  FakeConstraints constraints;
  constraints.AddMandatory(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp,
                           true);
  CreatePeerConnection(&constraints);
  // Create an offer just to ensure we have an identity before we manually
  // call SetLocalDescription.
  rtc::scoped_ptr<SessionDescriptionInterface> throwaway;
  ASSERT_TRUE(DoCreateOffer(throwaway.accept(), nullptr));

  rtc::scoped_ptr<SessionDescriptionInterface> desc;
  CreateSessionDescriptionAndReference(1, 1, desc.accept());
  std::string sdp;
  desc->ToString(&sdp);

  pc_->AddStream(reference_collection_->at(0));
  EXPECT_TRUE(DoSetLocalDescription(desc.release()));
  auto senders = pc_->GetSenders();
  EXPECT_EQ(2u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));

  // Add a new MediaStream but with the same tracks as in the first stream.
  rtc::scoped_refptr<webrtc::MediaStreamInterface> stream_1(
      webrtc::MediaStream::Create(kStreams[1]));
  stream_1->AddTrack(reference_collection_->at(0)->GetVideoTracks()[0]);
  stream_1->AddTrack(reference_collection_->at(0)->GetAudioTracks()[0]);
  pc_->AddStream(stream_1);

  // Replace msid in the original SDP.
  rtc::replace_substrs(kStreams[0], strlen(kStreams[0]), kStreams[1],
                       strlen(kStreams[1]), &sdp);

  rtc::scoped_ptr<SessionDescriptionInterface> updated_desc(
      webrtc::CreateSessionDescription(SessionDescriptionInterface::kOffer, sdp,
                                       nullptr));

  EXPECT_TRUE(DoSetLocalDescription(updated_desc.release()));
  senders = pc_->GetSenders();
  EXPECT_EQ(2u, senders.size());
  EXPECT_TRUE(ContainsSender(senders, kAudioTracks[0]));
  EXPECT_TRUE(ContainsSender(senders, kVideoTracks[0]));
}

// The following tests verify that session options are created correctly.
// TODO(deadbeef): Convert these tests to be more end-to-end. Instead of
// "verify options are converted correctly", should be "pass options into
// CreateOffer and verify the correct offer is produced."

TEST(CreateSessionOptionsTest, GetOptionsForOfferWithInvalidAudioOption) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.offer_to_receive_audio = RTCOfferAnswerOptions::kUndefined - 1;

  cricket::MediaSessionOptions options;
  EXPECT_FALSE(ConvertRtcOptionsForOffer(rtc_options, &options));

  rtc_options.offer_to_receive_audio =
      RTCOfferAnswerOptions::kMaxOfferToReceiveMedia + 1;
  EXPECT_FALSE(ConvertRtcOptionsForOffer(rtc_options, &options));
}

TEST(CreateSessionOptionsTest, GetOptionsForOfferWithInvalidVideoOption) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.offer_to_receive_video = RTCOfferAnswerOptions::kUndefined - 1;

  cricket::MediaSessionOptions options;
  EXPECT_FALSE(ConvertRtcOptionsForOffer(rtc_options, &options));

  rtc_options.offer_to_receive_video =
      RTCOfferAnswerOptions::kMaxOfferToReceiveMedia + 1;
  EXPECT_FALSE(ConvertRtcOptionsForOffer(rtc_options, &options));
}

// Test that a MediaSessionOptions is created for an offer if
// OfferToReceiveAudio and OfferToReceiveVideo options are set.
TEST(CreateSessionOptionsTest, GetMediaSessionOptionsForOfferWithAudioVideo) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.offer_to_receive_audio = 1;
  rtc_options.offer_to_receive_video = 1;

  cricket::MediaSessionOptions options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_TRUE(options.has_audio());
  EXPECT_TRUE(options.has_video());
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// OfferToReceiveAudio is set.
TEST(CreateSessionOptionsTest, GetMediaSessionOptionsForOfferWithAudio) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.offer_to_receive_audio = 1;

  cricket::MediaSessionOptions options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_TRUE(options.has_audio());
  EXPECT_FALSE(options.has_video());
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// the default OfferOptions are used.
TEST(CreateSessionOptionsTest, GetDefaultMediaSessionOptionsForOffer) {
  RTCOfferAnswerOptions rtc_options;

  cricket::MediaSessionOptions options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_TRUE(options.has_audio());
  EXPECT_FALSE(options.has_video());
  EXPECT_TRUE(options.bundle_enabled);
  EXPECT_TRUE(options.vad_enabled);
  EXPECT_FALSE(options.transport_options.ice_restart);
}

// Test that a correct MediaSessionOptions is created for an offer if
// OfferToReceiveVideo is set.
TEST(CreateSessionOptionsTest, GetMediaSessionOptionsForOfferWithVideo) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.offer_to_receive_audio = 0;
  rtc_options.offer_to_receive_video = 1;

  cricket::MediaSessionOptions options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_FALSE(options.has_audio());
  EXPECT_TRUE(options.has_video());
  EXPECT_TRUE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created for an offer if
// UseRtpMux is set to false.
TEST(CreateSessionOptionsTest,
     GetMediaSessionOptionsForOfferWithBundleDisabled) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.offer_to_receive_audio = 1;
  rtc_options.offer_to_receive_video = 1;
  rtc_options.use_rtp_mux = false;

  cricket::MediaSessionOptions options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_TRUE(options.has_audio());
  EXPECT_TRUE(options.has_video());
  EXPECT_FALSE(options.bundle_enabled);
}

// Test that a correct MediaSessionOptions is created to restart ice if
// IceRestart is set. It also tests that subsequent MediaSessionOptions don't
// have |transport_options.ice_restart| set.
TEST(CreateSessionOptionsTest, GetMediaSessionOptionsForOfferWithIceRestart) {
  RTCOfferAnswerOptions rtc_options;
  rtc_options.ice_restart = true;

  cricket::MediaSessionOptions options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_TRUE(options.transport_options.ice_restart);

  rtc_options = RTCOfferAnswerOptions();
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_options, &options));
  EXPECT_FALSE(options.transport_options.ice_restart);
}

// Test that the MediaConstraints in an answer don't affect if audio and video
// is offered in an offer but that if kOfferToReceiveAudio or
// kOfferToReceiveVideo constraints are true in an offer, the media type will be
// included in subsequent answers.
TEST(CreateSessionOptionsTest, MediaConstraintsInAnswer) {
  FakeConstraints answer_c;
  answer_c.SetMandatoryReceiveAudio(true);
  answer_c.SetMandatoryReceiveVideo(true);

  cricket::MediaSessionOptions answer_options;
  EXPECT_TRUE(ParseConstraintsForAnswer(&answer_c, &answer_options));
  EXPECT_TRUE(answer_options.has_audio());
  EXPECT_TRUE(answer_options.has_video());

  RTCOfferAnswerOptions rtc_offer_options;

  cricket::MediaSessionOptions offer_options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(rtc_offer_options, &offer_options));
  EXPECT_TRUE(offer_options.has_audio());
  EXPECT_FALSE(offer_options.has_video());

  RTCOfferAnswerOptions updated_rtc_offer_options;
  updated_rtc_offer_options.offer_to_receive_audio = 1;
  updated_rtc_offer_options.offer_to_receive_video = 1;

  cricket::MediaSessionOptions updated_offer_options;
  EXPECT_TRUE(ConvertRtcOptionsForOffer(updated_rtc_offer_options,
                                        &updated_offer_options));
  EXPECT_TRUE(updated_offer_options.has_audio());
  EXPECT_TRUE(updated_offer_options.has_video());

  // Since an offer has been created with both audio and video, subsequent
  // offers and answers should contain both audio and video.
  // Answers will only contain the media types that exist in the offer
  // regardless of the value of |updated_answer_options.has_audio| and
  // |updated_answer_options.has_video|.
  FakeConstraints updated_answer_c;
  answer_c.SetMandatoryReceiveAudio(false);
  answer_c.SetMandatoryReceiveVideo(false);

  cricket::MediaSessionOptions updated_answer_options;
  EXPECT_TRUE(
      ParseConstraintsForAnswer(&updated_answer_c, &updated_answer_options));
  EXPECT_TRUE(updated_answer_options.has_audio());
  EXPECT_TRUE(updated_answer_options.has_video());
}
