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

#include <vector>

#include "talk/app/webrtc/audiotrack.h"
#include "talk/app/webrtc/fakemediacontroller.h"
#include "talk/app/webrtc/fakemetricsobserver.h"
#include "talk/app/webrtc/jsepicecandidate.h"
#include "talk/app/webrtc/jsepsessiondescription.h"
#include "talk/app/webrtc/peerconnection.h"
#include "talk/app/webrtc/sctputils.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/streamcollection.h"
#include "talk/app/webrtc/test/fakeconstraints.h"
#include "talk/app/webrtc/test/fakedtlsidentitystore.h"
#include "talk/app/webrtc/videotrack.h"
#include "talk/app/webrtc/webrtcsession.h"
#include "talk/app/webrtc/webrtcsessiondescriptionfactory.h"
#include "talk/media/base/fakemediaengine.h"
#include "talk/media/base/fakevideorenderer.h"
#include "talk/media/base/mediachannel.h"
#include "talk/media/webrtc/fakewebrtccall.h"
#include "webrtc/p2p/base/stunserver.h"
#include "webrtc/p2p/base/teststunserver.h"
#include "webrtc/p2p/base/testturnserver.h"
#include "webrtc/p2p/base/transportchannel.h"
#include "webrtc/p2p/client/basicportallocator.h"
#include "talk/session/media/channelmanager.h"
#include "talk/session/media/mediasession.h"
#include "webrtc/base/fakenetwork.h"
#include "webrtc/base/firewallsocketserver.h"
#include "webrtc/base/gunit.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/network.h"
#include "webrtc/base/physicalsocketserver.h"
#include "webrtc/base/ssladapter.h"
#include "webrtc/base/sslidentity.h"
#include "webrtc/base/sslstreamadapter.h"
#include "webrtc/base/stringutils.h"
#include "webrtc/base/thread.h"
#include "webrtc/base/virtualsocketserver.h"

#define MAYBE_SKIP_TEST(feature)                    \
  if (!(feature())) {                               \
    LOG(LS_INFO) << "Feature disabled... skipping"; \
    return;                                         \
  }

using cricket::DF_PLAY;
using cricket::DF_SEND;
using cricket::FakeVoiceMediaChannel;
using cricket::TransportInfo;
using rtc::SocketAddress;
using rtc::scoped_ptr;
using rtc::Thread;
using webrtc::CreateSessionDescription;
using webrtc::CreateSessionDescriptionObserver;
using webrtc::CreateSessionDescriptionRequest;
using webrtc::DataChannel;
using webrtc::DtlsIdentityStoreInterface;
using webrtc::FakeConstraints;
using webrtc::FakeMetricsObserver;
using webrtc::IceCandidateCollection;
using webrtc::InternalDataChannelInit;
using webrtc::JsepIceCandidate;
using webrtc::JsepSessionDescription;
using webrtc::PeerConnectionFactoryInterface;
using webrtc::PeerConnectionInterface;
using webrtc::SessionDescriptionInterface;
using webrtc::SessionStats;
using webrtc::StreamCollection;
using webrtc::WebRtcSession;
using webrtc::kBundleWithoutRtcpMux;
using webrtc::kCreateChannelFailed;
using webrtc::kInvalidSdp;
using webrtc::kMlineMismatch;
using webrtc::kPushDownTDFailed;
using webrtc::kSdpWithoutIceUfragPwd;
using webrtc::kSdpWithoutDtlsFingerprint;
using webrtc::kSdpWithoutSdesCrypto;
using webrtc::kSessionError;
using webrtc::kSessionErrorDesc;
using webrtc::kMaxUnsignalledRecvStreams;

typedef PeerConnectionInterface::RTCOfferAnswerOptions RTCOfferAnswerOptions;

static const int kClientAddrPort = 0;
static const char kClientAddrHost1[] = "11.11.11.11";
static const char kClientIPv6AddrHost1[] =
    "2620:0:aaaa:bbbb:cccc:dddd:eeee:ffff";
static const char kClientAddrHost2[] = "22.22.22.22";
static const char kStunAddrHost[] = "99.99.99.1";
static const SocketAddress kTurnUdpIntAddr("99.99.99.4", 3478);
static const SocketAddress kTurnUdpExtAddr("99.99.99.6", 0);
static const char kTurnUsername[] = "test";
static const char kTurnPassword[] = "test";

static const char kSessionVersion[] = "1";

// Media index of candidates belonging to the first media content.
static const int kMediaContentIndex0 = 0;
static const char kMediaContentName0[] = "audio";

// Media index of candidates belonging to the second media content.
static const int kMediaContentIndex1 = 1;
static const char kMediaContentName1[] = "video";

static const int kIceCandidatesTimeout = 10000;

static const char kFakeDtlsFingerprint[] =
    "BB:CD:72:F7:2F:D0:BA:43:F3:68:B1:0C:23:72:B6:4A:"
    "0F:DE:34:06:BC:E0:FE:01:BC:73:C8:6D:F4:65:D5:24";

static const char kTooLongIceUfragPwd[] =
    "IceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfrag"
    "IceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfrag"
    "IceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfrag"
    "IceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfragIceUfrag";

static const char kSdpWithRtx[] =
    "v=0\r\n"
    "o=- 4104004319237231850 2 IN IP4 127.0.0.1\r\n"
    "s=-\r\n"
    "t=0 0\r\n"
    "a=msid-semantic: WMS stream1\r\n"
    "m=video 9 RTP/SAVPF 0 96\r\n"
    "c=IN IP4 0.0.0.0\r\n"
    "a=rtcp:9 IN IP4 0.0.0.0\r\n"
    "a=ice-ufrag:CerjGp19G7wpXwl7\r\n"
    "a=ice-pwd:cMvOlFvQ6ochez1ZOoC2uBEC\r\n"
    "a=mid:video\r\n"
    "a=sendrecv\r\n"
    "a=rtcp-mux\r\n"
    "a=crypto:1 AES_CM_128_HMAC_SHA1_80 "
    "inline:5/4N5CDvMiyDArHtBByUM71VIkguH17ZNoX60GrA\r\n"
    "a=rtpmap:0 fake_video_codec/90000\r\n"
    "a=rtpmap:96 rtx/90000\r\n"
    "a=fmtp:96 apt=0\r\n";

static const char kStream1[] = "stream1";
static const char kVideoTrack1[] = "video1";
static const char kAudioTrack1[] = "audio1";

static const char kStream2[] = "stream2";
static const char kVideoTrack2[] = "video2";
static const char kAudioTrack2[] = "audio2";

enum RTCCertificateGenerationMethod { ALREADY_GENERATED, DTLS_IDENTITY_STORE };

// Add some extra |newlines| to the |message| after |line|.
static void InjectAfter(const std::string& line,
                        const std::string& newlines,
                        std::string* message) {
  const std::string tmp = line + newlines;
  rtc::replace_substrs(line.c_str(), line.length(), tmp.c_str(), tmp.length(),
                       message);
}

class MockIceObserver : public webrtc::IceObserver {
 public:
  MockIceObserver()
      : oncandidatesready_(false),
        ice_connection_state_(PeerConnectionInterface::kIceConnectionNew),
        ice_gathering_state_(PeerConnectionInterface::kIceGatheringNew) {
  }

  virtual void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) {
    ice_connection_state_ = new_state;
  }
  virtual void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) {
    // We can never transition back to "new".
    EXPECT_NE(PeerConnectionInterface::kIceGatheringNew, new_state);
    ice_gathering_state_ = new_state;

    // oncandidatesready_ really means "ICE gathering is complete".
    // This if statement ensures that this value remains correct when we
    // transition from kIceGatheringComplete to kIceGatheringGathering.
    if (new_state == PeerConnectionInterface::kIceGatheringGathering) {
      oncandidatesready_ = false;
    }
  }

  // Found a new candidate.
  virtual void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    switch (candidate->sdp_mline_index()) {
      case kMediaContentIndex0:
        mline_0_candidates_.push_back(candidate->candidate());
        break;
      case kMediaContentIndex1:
        mline_1_candidates_.push_back(candidate->candidate());
        break;
      default:
        ASSERT(false);
    }

    // The ICE gathering state should always be Gathering when a candidate is
    // received (or possibly Completed in the case of the final candidate).
    EXPECT_NE(PeerConnectionInterface::kIceGatheringNew, ice_gathering_state_);
  }

  // TODO(bemasc): Remove this once callers transition to OnIceGatheringChange.
  virtual void OnIceComplete() {
    EXPECT_FALSE(oncandidatesready_);
    oncandidatesready_ = true;

    // OnIceGatheringChange(IceGatheringCompleted) and OnIceComplete() should
    // be called approximately simultaneously.  For ease of testing, this
    // check additionally requires that they be called in the above order.
    EXPECT_EQ(PeerConnectionInterface::kIceGatheringComplete,
              ice_gathering_state_);
  }

  bool oncandidatesready_;
  std::vector<cricket::Candidate> mline_0_candidates_;
  std::vector<cricket::Candidate> mline_1_candidates_;
  PeerConnectionInterface::IceConnectionState ice_connection_state_;
  PeerConnectionInterface::IceGatheringState ice_gathering_state_;
};

class WebRtcSessionForTest : public webrtc::WebRtcSession {
 public:
  WebRtcSessionForTest(webrtc::MediaControllerInterface* media_controller,
                       rtc::Thread* signaling_thread,
                       rtc::Thread* worker_thread,
                       cricket::PortAllocator* port_allocator,
                       webrtc::IceObserver* ice_observer)
      : WebRtcSession(media_controller,
                      signaling_thread,
                      worker_thread,
                      port_allocator) {
    RegisterIceObserver(ice_observer);
  }
  virtual ~WebRtcSessionForTest() {}

  // Note that these methods are only safe to use if the signaling thread
  // is the same as the worker thread
  cricket::TransportChannel* voice_rtp_transport_channel() {
    return rtp_transport_channel(voice_channel());
  }

  cricket::TransportChannel* voice_rtcp_transport_channel() {
    return rtcp_transport_channel(voice_channel());
  }

  cricket::TransportChannel* video_rtp_transport_channel() {
    return rtp_transport_channel(video_channel());
  }

  cricket::TransportChannel* video_rtcp_transport_channel() {
    return rtcp_transport_channel(video_channel());
  }

  cricket::TransportChannel* data_rtp_transport_channel() {
    return rtp_transport_channel(data_channel());
  }

  cricket::TransportChannel* data_rtcp_transport_channel() {
    return rtcp_transport_channel(data_channel());
  }

  using webrtc::WebRtcSession::SetAudioPlayout;
  using webrtc::WebRtcSession::SetAudioSend;
  using webrtc::WebRtcSession::SetCaptureDevice;
  using webrtc::WebRtcSession::SetVideoPlayout;
  using webrtc::WebRtcSession::SetVideoSend;

 private:
  cricket::TransportChannel* rtp_transport_channel(cricket::BaseChannel* ch) {
    if (!ch) {
      return nullptr;
    }
    return ch->transport_channel();
  }

  cricket::TransportChannel* rtcp_transport_channel(cricket::BaseChannel* ch) {
    if (!ch) {
      return nullptr;
    }
    return ch->rtcp_transport_channel();
  }
};

class WebRtcSessionCreateSDPObserverForTest
    : public rtc::RefCountedObject<CreateSessionDescriptionObserver> {
 public:
  enum State {
    kInit,
    kFailed,
    kSucceeded,
  };
  WebRtcSessionCreateSDPObserverForTest() : state_(kInit) {}

  // CreateSessionDescriptionObserver implementation.
  virtual void OnSuccess(SessionDescriptionInterface* desc) {
    description_.reset(desc);
    state_ = kSucceeded;
  }
  virtual void OnFailure(const std::string& error) {
    state_ = kFailed;
  }

  SessionDescriptionInterface* description() { return description_.get(); }

  SessionDescriptionInterface* ReleaseDescription() {
    return description_.release();
  }

  State state() const { return state_; }

 protected:
  ~WebRtcSessionCreateSDPObserverForTest() {}

 private:
  rtc::scoped_ptr<SessionDescriptionInterface> description_;
  State state_;
};

class FakeAudioRenderer : public cricket::AudioRenderer {
 public:
  FakeAudioRenderer() : sink_(NULL) {}
  virtual ~FakeAudioRenderer() {
    if (sink_)
      sink_->OnClose();
  }

  void SetSink(Sink* sink) override { sink_ = sink; }

  cricket::AudioRenderer::Sink* sink() const { return sink_; }
 private:
  cricket::AudioRenderer::Sink* sink_;
};

class WebRtcSessionTest
    : public testing::TestWithParam<RTCCertificateGenerationMethod>,
      public sigslot::has_slots<> {
 protected:
  // TODO Investigate why ChannelManager crashes, if it's created
  // after stun_server.
  WebRtcSessionTest()
      : media_engine_(new cricket::FakeMediaEngine()),
        data_engine_(new cricket::FakeDataEngine()),
        channel_manager_(
            new cricket::ChannelManager(media_engine_,
                                        data_engine_,
                                        new cricket::CaptureManager(),
                                        rtc::Thread::Current())),
        fake_call_(webrtc::Call::Config()),
        media_controller_(
            webrtc::MediaControllerInterface::Create(rtc::Thread::Current(),
                                                     channel_manager_.get())),
        tdesc_factory_(new cricket::TransportDescriptionFactory()),
        desc_factory_(
            new cricket::MediaSessionDescriptionFactory(channel_manager_.get(),
                                                        tdesc_factory_.get())),
        pss_(new rtc::PhysicalSocketServer),
        vss_(new rtc::VirtualSocketServer(pss_.get())),
        fss_(new rtc::FirewallSocketServer(vss_.get())),
        ss_scope_(fss_.get()),
        stun_socket_addr_(
            rtc::SocketAddress(kStunAddrHost, cricket::STUN_SERVER_PORT)),
        stun_server_(cricket::TestStunServer::Create(Thread::Current(),
                                                     stun_socket_addr_)),
        turn_server_(Thread::Current(), kTurnUdpIntAddr, kTurnUdpExtAddr),
        metrics_observer_(new rtc::RefCountedObject<FakeMetricsObserver>()) {
    cricket::ServerAddresses stun_servers;
    stun_servers.insert(stun_socket_addr_);
    allocator_.reset(new cricket::BasicPortAllocator(
        &network_manager_,
        stun_servers,
        SocketAddress(), SocketAddress(), SocketAddress()));
    allocator_->set_flags(cricket::PORTALLOCATOR_DISABLE_TCP |
                          cricket::PORTALLOCATOR_DISABLE_RELAY);
    EXPECT_TRUE(channel_manager_->Init());
    desc_factory_->set_add_legacy_streams(false);
    allocator_->set_step_delay(cricket::kMinimumStepDelay);
  }

  void AddInterface(const SocketAddress& addr) {
    network_manager_.AddInterface(addr);
  }

  // If |dtls_identity_store| != null or |rtc_configuration| contains
  // |certificates| then DTLS will be enabled unless explicitly disabled by
  // |rtc_configuration| options. When DTLS is enabled a certificate will be
  // used if provided, otherwise one will be generated using the
  // |dtls_identity_store|.
  void Init(
      rtc::scoped_ptr<webrtc::DtlsIdentityStoreInterface> dtls_identity_store,
      const PeerConnectionInterface::RTCConfiguration& rtc_configuration) {
    ASSERT_TRUE(session_.get() == NULL);
    session_.reset(new WebRtcSessionForTest(
        media_controller_.get(), rtc::Thread::Current(), rtc::Thread::Current(),
        allocator_.get(), &observer_));
    session_->SignalDataChannelOpenMessage.connect(
        this, &WebRtcSessionTest::OnDataChannelOpenMessage);

    EXPECT_EQ(PeerConnectionInterface::kIceConnectionNew,
        observer_.ice_connection_state_);
    EXPECT_EQ(PeerConnectionInterface::kIceGatheringNew,
        observer_.ice_gathering_state_);

    EXPECT_TRUE(session_->Initialize(options_, constraints_.get(),
                                     dtls_identity_store.Pass(),
                                     rtc_configuration));
    session_->set_metrics_observer(metrics_observer_);
  }

  void OnDataChannelOpenMessage(const std::string& label,
                                const InternalDataChannelInit& config) {
    last_data_channel_label_ = label;
    last_data_channel_config_ = config;
  }

  void Init() {
    PeerConnectionInterface::RTCConfiguration configuration;
    Init(nullptr, configuration);
  }

  void InitWithIceTransport(
      PeerConnectionInterface::IceTransportsType ice_transport_type) {
    PeerConnectionInterface::RTCConfiguration configuration;
    configuration.type = ice_transport_type;
    Init(nullptr, configuration);
  }

  void InitWithBundlePolicy(
      PeerConnectionInterface::BundlePolicy bundle_policy) {
    PeerConnectionInterface::RTCConfiguration configuration;
    configuration.bundle_policy = bundle_policy;
    Init(nullptr, configuration);
  }

  void InitWithRtcpMuxPolicy(
      PeerConnectionInterface::RtcpMuxPolicy rtcp_mux_policy) {
    PeerConnectionInterface::RTCConfiguration configuration;
    configuration.rtcp_mux_policy = rtcp_mux_policy;
    Init(nullptr, configuration);
  }

  // Successfully init with DTLS; with a certificate generated and supplied or
  // with a store that generates it for us.
  void InitWithDtls(RTCCertificateGenerationMethod cert_gen_method) {
    rtc::scoped_ptr<FakeDtlsIdentityStore> dtls_identity_store;
    PeerConnectionInterface::RTCConfiguration configuration;
    if (cert_gen_method == ALREADY_GENERATED) {
      configuration.certificates.push_back(
          FakeDtlsIdentityStore::GenerateCertificate());
    } else if (cert_gen_method == DTLS_IDENTITY_STORE) {
      dtls_identity_store.reset(new FakeDtlsIdentityStore());
      dtls_identity_store->set_should_fail(false);
    } else {
      RTC_CHECK(false);
    }
    Init(dtls_identity_store.Pass(), configuration);
  }

  // Init with DTLS with a store that will fail to generate a certificate.
  void InitWithDtlsIdentityGenFail() {
    rtc::scoped_ptr<FakeDtlsIdentityStore> dtls_identity_store(
        new FakeDtlsIdentityStore());
    dtls_identity_store->set_should_fail(true);
    PeerConnectionInterface::RTCConfiguration configuration;
    Init(dtls_identity_store.Pass(), configuration);
  }

  void InitWithDtmfCodec() {
    // Add kTelephoneEventCodec for dtmf test.
    const cricket::AudioCodec kTelephoneEventCodec(
        106, "telephone-event", 8000, 0, 1, 0);
    std::vector<cricket::AudioCodec> codecs;
    codecs.push_back(kTelephoneEventCodec);
    media_engine_->SetAudioCodecs(codecs);
    desc_factory_->set_audio_codecs(codecs);
    Init();
  }

  void SendAudioVideoStream1() {
    send_stream_1_ = true;
    send_stream_2_ = false;
    send_audio_ = true;
    send_video_ = true;
  }

  void SendAudioVideoStream2() {
    send_stream_1_ = false;
    send_stream_2_ = true;
    send_audio_ = true;
    send_video_ = true;
  }

  void SendAudioVideoStream1And2() {
    send_stream_1_ = true;
    send_stream_2_ = true;
    send_audio_ = true;
    send_video_ = true;
  }

  void SendNothing() {
    send_stream_1_ = false;
    send_stream_2_ = false;
    send_audio_ = false;
    send_video_ = false;
  }

  void SendAudioOnlyStream2() {
    send_stream_1_ = false;
    send_stream_2_ = true;
    send_audio_ = true;
    send_video_ = false;
  }

  void SendVideoOnlyStream2() {
    send_stream_1_ = false;
    send_stream_2_ = true;
    send_audio_ = false;
    send_video_ = true;
  }

  void AddStreamsToOptions(cricket::MediaSessionOptions* session_options) {
    if (send_stream_1_ && send_audio_) {
      session_options->AddSendStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack1,
                                     kStream1);
    }
    if (send_stream_1_ && send_video_) {
      session_options->AddSendStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack1,
                                     kStream1);
    }
    if (send_stream_2_ && send_audio_) {
      session_options->AddSendStream(cricket::MEDIA_TYPE_AUDIO, kAudioTrack2,
                                     kStream2);
    }
    if (send_stream_2_ && send_video_) {
      session_options->AddSendStream(cricket::MEDIA_TYPE_VIDEO, kVideoTrack2,
                                     kStream2);
    }
    if (data_channel_ && session_->data_channel_type() == cricket::DCT_RTP) {
      session_options->AddSendStream(cricket::MEDIA_TYPE_DATA,
                                     data_channel_->label(),
                                     data_channel_->label());
    }
  }

  void GetOptionsForOffer(
      const PeerConnectionInterface::RTCOfferAnswerOptions& rtc_options,
      cricket::MediaSessionOptions* session_options) {
    ASSERT_TRUE(ConvertRtcOptionsForOffer(rtc_options, session_options));

    AddStreamsToOptions(session_options);
    if (rtc_options.offer_to_receive_audio ==
        RTCOfferAnswerOptions::kUndefined) {
      session_options->recv_audio =
          session_options->HasSendMediaStream(cricket::MEDIA_TYPE_AUDIO);
    }
    if (rtc_options.offer_to_receive_video ==
        RTCOfferAnswerOptions::kUndefined) {
      session_options->recv_video =
          session_options->HasSendMediaStream(cricket::MEDIA_TYPE_VIDEO);
    }
    session_options->bundle_enabled =
        session_options->bundle_enabled &&
        (session_options->has_audio() || session_options->has_video() ||
         session_options->has_data());

    if (session_->data_channel_type() == cricket::DCT_SCTP && data_channel_) {
      session_options->data_channel_type = cricket::DCT_SCTP;
    }
  }

  void GetOptionsForAnswer(const webrtc::MediaConstraintsInterface* constraints,
                           cricket::MediaSessionOptions* session_options) {
    session_options->recv_audio = false;
    session_options->recv_video = false;
    ASSERT_TRUE(ParseConstraintsForAnswer(constraints, session_options));

    AddStreamsToOptions(session_options);
    session_options->bundle_enabled =
        session_options->bundle_enabled &&
        (session_options->has_audio() || session_options->has_video() ||
         session_options->has_data());

    if (session_->data_channel_type() == cricket::DCT_SCTP) {
      session_options->data_channel_type = cricket::DCT_SCTP;
    }
  }

  // Creates a local offer and applies it. Starts ICE.
  // Call SendAudioVideoStreamX() before this function
  // to decide which streams to create.
  void InitiateCall() {
    SessionDescriptionInterface* offer = CreateOffer();
    SetLocalDescriptionWithoutError(offer);
    EXPECT_TRUE_WAIT(PeerConnectionInterface::kIceGatheringNew !=
        observer_.ice_gathering_state_,
        kIceCandidatesTimeout);
  }

  SessionDescriptionInterface* CreateOffer() {
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_audio =
        RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;

    return CreateOffer(options);
  }

  SessionDescriptionInterface* CreateOffer(
      const PeerConnectionInterface::RTCOfferAnswerOptions& options) {
    rtc::scoped_refptr<WebRtcSessionCreateSDPObserverForTest>
        observer = new WebRtcSessionCreateSDPObserverForTest();
    cricket::MediaSessionOptions session_options;
    GetOptionsForOffer(options, &session_options);
    session_->CreateOffer(observer, options, session_options);
    EXPECT_TRUE_WAIT(
        observer->state() != WebRtcSessionCreateSDPObserverForTest::kInit,
        2000);
    return observer->ReleaseDescription();
  }

  SessionDescriptionInterface* CreateAnswer(
      const webrtc::MediaConstraintsInterface* constraints) {
    rtc::scoped_refptr<WebRtcSessionCreateSDPObserverForTest> observer
        = new WebRtcSessionCreateSDPObserverForTest();
    cricket::MediaSessionOptions session_options;
    GetOptionsForAnswer(constraints, &session_options);
    session_->CreateAnswer(observer, constraints, session_options);
    EXPECT_TRUE_WAIT(
        observer->state() != WebRtcSessionCreateSDPObserverForTest::kInit,
        2000);
    return observer->ReleaseDescription();
  }

  bool ChannelsExist() const {
    return (session_->voice_channel() != NULL &&
            session_->video_channel() != NULL);
  }

  void VerifyCryptoParams(const cricket::SessionDescription* sdp) {
    ASSERT_TRUE(session_.get() != NULL);
    const cricket::ContentInfo* content = cricket::GetFirstAudioContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::AudioContentDescription* audio_content =
        static_cast<const cricket::AudioContentDescription*>(
            content->description);
    ASSERT_TRUE(audio_content != NULL);
    ASSERT_EQ(1U, audio_content->cryptos().size());
    ASSERT_EQ(47U, audio_content->cryptos()[0].key_params.size());
    ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
              audio_content->cryptos()[0].cipher_suite);
    EXPECT_EQ(std::string(cricket::kMediaProtocolSavpf),
              audio_content->protocol());

    content = cricket::GetFirstVideoContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::VideoContentDescription* video_content =
        static_cast<const cricket::VideoContentDescription*>(
            content->description);
    ASSERT_TRUE(video_content != NULL);
    ASSERT_EQ(1U, video_content->cryptos().size());
    ASSERT_EQ("AES_CM_128_HMAC_SHA1_80",
              video_content->cryptos()[0].cipher_suite);
    ASSERT_EQ(47U, video_content->cryptos()[0].key_params.size());
    EXPECT_EQ(std::string(cricket::kMediaProtocolSavpf),
              video_content->protocol());
  }

  void VerifyNoCryptoParams(const cricket::SessionDescription* sdp, bool dtls) {
    const cricket::ContentInfo* content = cricket::GetFirstAudioContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::AudioContentDescription* audio_content =
        static_cast<const cricket::AudioContentDescription*>(
            content->description);
    ASSERT_TRUE(audio_content != NULL);
    ASSERT_EQ(0U, audio_content->cryptos().size());

    content = cricket::GetFirstVideoContent(sdp);
    ASSERT_TRUE(content != NULL);
    const cricket::VideoContentDescription* video_content =
        static_cast<const cricket::VideoContentDescription*>(
            content->description);
    ASSERT_TRUE(video_content != NULL);
    ASSERT_EQ(0U, video_content->cryptos().size());

    if (dtls) {
      EXPECT_EQ(std::string(cricket::kMediaProtocolDtlsSavpf),
                audio_content->protocol());
      EXPECT_EQ(std::string(cricket::kMediaProtocolDtlsSavpf),
                video_content->protocol());
    } else {
      EXPECT_EQ(std::string(cricket::kMediaProtocolAvpf),
                audio_content->protocol());
      EXPECT_EQ(std::string(cricket::kMediaProtocolAvpf),
                video_content->protocol());
    }
  }

  // Set the internal fake description factories to do DTLS-SRTP.
  void SetFactoryDtlsSrtp() {
    desc_factory_->set_secure(cricket::SEC_DISABLED);
    std::string identity_name = "WebRTC" +
        rtc::ToString(rtc::CreateRandomId());
    // Confirmed to work with KT_RSA and KT_ECDSA.
    tdesc_factory_->set_certificate(rtc::RTCCertificate::Create(
        rtc::scoped_ptr<rtc::SSLIdentity>(rtc::SSLIdentity::Generate(
            identity_name, rtc::KT_DEFAULT)).Pass()));
    tdesc_factory_->set_secure(cricket::SEC_REQUIRED);
  }

  void VerifyFingerprintStatus(const cricket::SessionDescription* sdp,
                               bool expected) {
    const TransportInfo* audio = sdp->GetTransportInfoByName("audio");
    ASSERT_TRUE(audio != NULL);
    ASSERT_EQ(expected, audio->description.identity_fingerprint.get() != NULL);
    const TransportInfo* video = sdp->GetTransportInfoByName("video");
    ASSERT_TRUE(video != NULL);
    ASSERT_EQ(expected, video->description.identity_fingerprint.get() != NULL);
  }

  void VerifyAnswerFromNonCryptoOffer() {
    // Create an SDP without Crypto.
    cricket::MediaSessionOptions options;
    options.recv_video = true;
    JsepSessionDescription* offer(
        CreateRemoteOffer(options, cricket::SEC_DISABLED));
    ASSERT_TRUE(offer != NULL);
    VerifyNoCryptoParams(offer->description(), false);
    SetRemoteDescriptionOfferExpectError(kSdpWithoutSdesCrypto,
                                         offer);
    const webrtc::SessionDescriptionInterface* answer = CreateAnswer(NULL);
    // Answer should be NULL as no crypto params in offer.
    ASSERT_TRUE(answer == NULL);
  }

  void VerifyAnswerFromCryptoOffer() {
    cricket::MediaSessionOptions options;
    options.recv_video = true;
    options.bundle_enabled = true;
    scoped_ptr<JsepSessionDescription> offer(
        CreateRemoteOffer(options, cricket::SEC_REQUIRED));
    ASSERT_TRUE(offer.get() != NULL);
    VerifyCryptoParams(offer->description());
    SetRemoteDescriptionWithoutError(offer.release());
    scoped_ptr<SessionDescriptionInterface> answer(CreateAnswer(NULL));
    ASSERT_TRUE(answer.get() != NULL);
    VerifyCryptoParams(answer->description());
  }

  void SetAndVerifyNumUnsignalledRecvStreams(
      int value_set, int value_expected) {
    constraints_.reset(new FakeConstraints());
    constraints_->AddOptional(
        webrtc::MediaConstraintsInterface::kNumUnsignalledRecvStreams,
        value_set);
    session_.reset();
    Init();
    SendAudioVideoStream1();
    SessionDescriptionInterface* offer = CreateOffer();

    SetLocalDescriptionWithoutError(offer);

    video_channel_ = media_engine_->GetVideoChannel(0);

    ASSERT_TRUE(video_channel_ != NULL);
    const cricket::VideoOptions& video_options = video_channel_->options();
    EXPECT_EQ(value_expected,
              video_options.unsignalled_recv_stream_limit.value_or(-1));
  }

  void CompareIceUfragAndPassword(const cricket::SessionDescription* desc1,
                                  const cricket::SessionDescription* desc2,
                                  bool expect_equal) {
    if (desc1->contents().size() != desc2->contents().size()) {
      EXPECT_FALSE(expect_equal);
      return;
    }

    const cricket::ContentInfos& contents = desc1->contents();
    cricket::ContentInfos::const_iterator it = contents.begin();

    for (; it != contents.end(); ++it) {
      const cricket::TransportDescription* transport_desc1 =
          desc1->GetTransportDescriptionByName(it->name);
      const cricket::TransportDescription* transport_desc2 =
          desc2->GetTransportDescriptionByName(it->name);
      if (!transport_desc1 || !transport_desc2) {
        EXPECT_FALSE(expect_equal);
        return;
      }
      if (transport_desc1->ice_pwd != transport_desc2->ice_pwd ||
          transport_desc1->ice_ufrag != transport_desc2->ice_ufrag) {
        EXPECT_FALSE(expect_equal);
        return;
      }
    }
    EXPECT_TRUE(expect_equal);
  }

  void RemoveIceUfragPwdLines(const SessionDescriptionInterface* current_desc,
                              std::string *sdp) {
    const cricket::SessionDescription* desc = current_desc->description();
    EXPECT_TRUE(current_desc->ToString(sdp));

    const cricket::ContentInfos& contents = desc->contents();
    cricket::ContentInfos::const_iterator it = contents.begin();
    // Replace ufrag and pwd lines with empty strings.
    for (; it != contents.end(); ++it) {
      const cricket::TransportDescription* transport_desc =
          desc->GetTransportDescriptionByName(it->name);
      std::string ufrag_line = "a=ice-ufrag:" + transport_desc->ice_ufrag
          + "\r\n";
      std::string pwd_line = "a=ice-pwd:" + transport_desc->ice_pwd
          + "\r\n";
      rtc::replace_substrs(ufrag_line.c_str(), ufrag_line.length(),
                                 "", 0,
                                 sdp);
      rtc::replace_substrs(pwd_line.c_str(), pwd_line.length(),
                                 "", 0,
                                 sdp);
    }
  }

  void ModifyIceUfragPwdLines(const SessionDescriptionInterface* current_desc,
                              const std::string& modified_ice_ufrag,
                              const std::string& modified_ice_pwd,
                              std::string* sdp) {
    const cricket::SessionDescription* desc = current_desc->description();
    EXPECT_TRUE(current_desc->ToString(sdp));

    const cricket::ContentInfos& contents = desc->contents();
    cricket::ContentInfos::const_iterator it = contents.begin();
    // Replace ufrag and pwd lines with |modified_ice_ufrag| and
    // |modified_ice_pwd| strings.
    for (; it != contents.end(); ++it) {
      const cricket::TransportDescription* transport_desc =
          desc->GetTransportDescriptionByName(it->name);
      std::string ufrag_line = "a=ice-ufrag:" + transport_desc->ice_ufrag
          + "\r\n";
      std::string pwd_line = "a=ice-pwd:" + transport_desc->ice_pwd
          + "\r\n";
      std::string mod_ufrag = "a=ice-ufrag:" + modified_ice_ufrag + "\r\n";
      std::string mod_pwd = "a=ice-pwd:" + modified_ice_pwd + "\r\n";
      rtc::replace_substrs(ufrag_line.c_str(), ufrag_line.length(),
                                 mod_ufrag.c_str(), mod_ufrag.length(),
                                 sdp);
      rtc::replace_substrs(pwd_line.c_str(), pwd_line.length(),
                                 mod_pwd.c_str(), mod_pwd.length(),
                                 sdp);
    }
  }

  // Creates a remote offer and and applies it as a remote description,
  // creates a local answer and applies is as a local description.
  // Call SendAudioVideoStreamX() before this function
  // to decide which local and remote streams to create.
  void CreateAndSetRemoteOfferAndLocalAnswer() {
    SessionDescriptionInterface* offer = CreateRemoteOffer();
    SetRemoteDescriptionWithoutError(offer);
    SessionDescriptionInterface* answer = CreateAnswer(NULL);
    SetLocalDescriptionWithoutError(answer);
  }
  void SetLocalDescriptionWithoutError(SessionDescriptionInterface* desc) {
    EXPECT_TRUE(session_->SetLocalDescription(desc, NULL));
    session_->MaybeStartGathering();
  }
  void SetLocalDescriptionExpectState(SessionDescriptionInterface* desc,
                                      WebRtcSession::State expected_state) {
    SetLocalDescriptionWithoutError(desc);
    EXPECT_EQ(expected_state, session_->state());
  }
  void SetLocalDescriptionExpectError(const std::string& action,
                                      const std::string& expected_error,
                                      SessionDescriptionInterface* desc) {
    std::string error;
    EXPECT_FALSE(session_->SetLocalDescription(desc, &error));
    std::string sdp_type = "local ";
    sdp_type.append(action);
    EXPECT_NE(std::string::npos, error.find(sdp_type));
    EXPECT_NE(std::string::npos, error.find(expected_error));
  }
  void SetLocalDescriptionOfferExpectError(const std::string& expected_error,
                                           SessionDescriptionInterface* desc) {
    SetLocalDescriptionExpectError(SessionDescriptionInterface::kOffer,
                                   expected_error, desc);
  }
  void SetLocalDescriptionAnswerExpectError(const std::string& expected_error,
                                            SessionDescriptionInterface* desc) {
    SetLocalDescriptionExpectError(SessionDescriptionInterface::kAnswer,
                                   expected_error, desc);
  }
  void SetRemoteDescriptionWithoutError(SessionDescriptionInterface* desc) {
    EXPECT_TRUE(session_->SetRemoteDescription(desc, NULL));
  }
  void SetRemoteDescriptionExpectState(SessionDescriptionInterface* desc,
                                       WebRtcSession::State expected_state) {
    SetRemoteDescriptionWithoutError(desc);
    EXPECT_EQ(expected_state, session_->state());
  }
  void SetRemoteDescriptionExpectError(const std::string& action,
                                       const std::string& expected_error,
                                       SessionDescriptionInterface* desc) {
    std::string error;
    EXPECT_FALSE(session_->SetRemoteDescription(desc, &error));
    std::string sdp_type = "remote ";
    sdp_type.append(action);
    EXPECT_NE(std::string::npos, error.find(sdp_type));
    EXPECT_NE(std::string::npos, error.find(expected_error));
  }
  void SetRemoteDescriptionOfferExpectError(
      const std::string& expected_error, SessionDescriptionInterface* desc) {
    SetRemoteDescriptionExpectError(SessionDescriptionInterface::kOffer,
                                    expected_error, desc);
  }
  void SetRemoteDescriptionPranswerExpectError(
      const std::string& expected_error, SessionDescriptionInterface* desc) {
    SetRemoteDescriptionExpectError(SessionDescriptionInterface::kPrAnswer,
                                    expected_error, desc);
  }
  void SetRemoteDescriptionAnswerExpectError(
      const std::string& expected_error, SessionDescriptionInterface* desc) {
    SetRemoteDescriptionExpectError(SessionDescriptionInterface::kAnswer,
                                    expected_error, desc);
  }

  void CreateCryptoOfferAndNonCryptoAnswer(SessionDescriptionInterface** offer,
      SessionDescriptionInterface** nocrypto_answer) {
    // Create a SDP without Crypto.
    cricket::MediaSessionOptions options;
    options.recv_video = true;
    options.bundle_enabled = true;
    *offer = CreateRemoteOffer(options, cricket::SEC_ENABLED);
    ASSERT_TRUE(*offer != NULL);
    VerifyCryptoParams((*offer)->description());

    *nocrypto_answer = CreateRemoteAnswer(*offer, options,
                                          cricket::SEC_DISABLED);
    EXPECT_TRUE(*nocrypto_answer != NULL);
  }

  void CreateDtlsOfferAndNonDtlsAnswer(SessionDescriptionInterface** offer,
      SessionDescriptionInterface** nodtls_answer) {
    cricket::MediaSessionOptions options;
    options.recv_video = true;
    options.bundle_enabled = true;

    rtc::scoped_ptr<SessionDescriptionInterface> temp_offer(
        CreateRemoteOffer(options, cricket::SEC_ENABLED));

    *nodtls_answer =
        CreateRemoteAnswer(temp_offer.get(), options, cricket::SEC_ENABLED);
    EXPECT_TRUE(*nodtls_answer != NULL);
    VerifyFingerprintStatus((*nodtls_answer)->description(), false);
    VerifyCryptoParams((*nodtls_answer)->description());

    SetFactoryDtlsSrtp();
    *offer = CreateRemoteOffer(options, cricket::SEC_ENABLED);
    ASSERT_TRUE(*offer != NULL);
    VerifyFingerprintStatus((*offer)->description(), true);
    VerifyCryptoParams((*offer)->description());
  }

  JsepSessionDescription* CreateRemoteOfferWithVersion(
        cricket::MediaSessionOptions options,
        cricket::SecurePolicy secure_policy,
        const std::string& session_version,
        const SessionDescriptionInterface* current_desc) {
    std::string session_id = rtc::ToString(rtc::CreateRandomId64());
    const cricket::SessionDescription* cricket_desc = NULL;
    if (current_desc) {
      cricket_desc = current_desc->description();
      session_id = current_desc->session_id();
    }

    desc_factory_->set_secure(secure_policy);
    JsepSessionDescription* offer(
        new JsepSessionDescription(JsepSessionDescription::kOffer));
    if (!offer->Initialize(desc_factory_->CreateOffer(options, cricket_desc),
                           session_id, session_version)) {
      delete offer;
      offer = NULL;
    }
    return offer;
  }
  JsepSessionDescription* CreateRemoteOffer(
      cricket::MediaSessionOptions options) {
    return CreateRemoteOfferWithVersion(options, cricket::SEC_ENABLED,
                                        kSessionVersion, NULL);
  }
  JsepSessionDescription* CreateRemoteOffer(
      cricket::MediaSessionOptions options, cricket::SecurePolicy sdes_policy) {
    return CreateRemoteOfferWithVersion(
        options, sdes_policy, kSessionVersion, NULL);
  }
  JsepSessionDescription* CreateRemoteOffer(
      cricket::MediaSessionOptions options,
      const SessionDescriptionInterface* current_desc) {
    return CreateRemoteOfferWithVersion(options, cricket::SEC_ENABLED,
                                        kSessionVersion, current_desc);
  }

  JsepSessionDescription* CreateRemoteOfferWithSctpPort(
      const char* sctp_stream_name, int new_port,
      cricket::MediaSessionOptions options) {
    options.data_channel_type = cricket::DCT_SCTP;
    options.AddSendStream(cricket::MEDIA_TYPE_DATA, "datachannel",
                          sctp_stream_name);
    return ChangeSDPSctpPort(new_port, CreateRemoteOffer(options));
  }

  // Takes ownership of offer_basis (and deletes it).
  JsepSessionDescription* ChangeSDPSctpPort(
      int new_port, webrtc::SessionDescriptionInterface *offer_basis) {
    // Stringify the input SDP, swap the 5000 for 'new_port' and create a new
    // SessionDescription from the mutated string.
    const char* default_port_str = "5000";
    char new_port_str[16];
    rtc::sprintfn(new_port_str, sizeof(new_port_str), "%d", new_port);
    std::string offer_str;
    offer_basis->ToString(&offer_str);
    rtc::replace_substrs(default_port_str, strlen(default_port_str),
                               new_port_str, strlen(new_port_str),
                               &offer_str);
    JsepSessionDescription* offer = new JsepSessionDescription(
        offer_basis->type());
    delete offer_basis;
    offer->Initialize(offer_str, NULL);
    return offer;
  }

  // Create a remote offer. Call SendAudioVideoStreamX()
  // before this function to decide which streams to create.
  JsepSessionDescription* CreateRemoteOffer() {
    cricket::MediaSessionOptions options;
    GetOptionsForAnswer(NULL, &options);
    return CreateRemoteOffer(options, session_->remote_description());
  }

  JsepSessionDescription* CreateRemoteAnswer(
      const SessionDescriptionInterface* offer,
      cricket::MediaSessionOptions options,
      cricket::SecurePolicy policy) {
    desc_factory_->set_secure(policy);
    const std::string session_id =
        rtc::ToString(rtc::CreateRandomId64());
    JsepSessionDescription* answer(
        new JsepSessionDescription(JsepSessionDescription::kAnswer));
    if (!answer->Initialize(desc_factory_->CreateAnswer(offer->description(),
                                                        options, NULL),
                            session_id, kSessionVersion)) {
      delete answer;
      answer = NULL;
    }
    return answer;
  }

  JsepSessionDescription* CreateRemoteAnswer(
      const SessionDescriptionInterface* offer,
      cricket::MediaSessionOptions options) {
      return CreateRemoteAnswer(offer, options, cricket::SEC_REQUIRED);
  }

  // Creates an answer session description.
  // Call SendAudioVideoStreamX() before this function
  // to decide which streams to create.
  JsepSessionDescription* CreateRemoteAnswer(
      const SessionDescriptionInterface* offer) {
    cricket::MediaSessionOptions options;
    GetOptionsForAnswer(NULL, &options);
    return CreateRemoteAnswer(offer, options, cricket::SEC_REQUIRED);
  }

  void TestSessionCandidatesWithBundleRtcpMux(bool bundle, bool rtcp_mux) {
    AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
    Init();
    SendAudioVideoStream1();

    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.use_rtp_mux = bundle;

    SessionDescriptionInterface* offer = CreateOffer(options);
    // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
    // and answer.
    SetLocalDescriptionWithoutError(offer);

    rtc::scoped_ptr<SessionDescriptionInterface> answer(
        CreateRemoteAnswer(session_->local_description()));
    std::string sdp;
    EXPECT_TRUE(answer->ToString(&sdp));

    size_t expected_candidate_num = 2;
    if (!rtcp_mux) {
      // If rtcp_mux is enabled we should expect 4 candidates - host and srflex
      // for rtp and rtcp.
      expected_candidate_num = 4;
      // Disable rtcp-mux from the answer
      const std::string kRtcpMux = "a=rtcp-mux";
      const std::string kXRtcpMux = "a=xrtcp-mux";
      rtc::replace_substrs(kRtcpMux.c_str(), kRtcpMux.length(),
                                 kXRtcpMux.c_str(), kXRtcpMux.length(),
                                 &sdp);
    }

    SessionDescriptionInterface* new_answer = CreateSessionDescription(
        JsepSessionDescription::kAnswer, sdp, NULL);

    // SetRemoteDescription to enable rtcp mux.
    SetRemoteDescriptionWithoutError(new_answer);
    EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
    EXPECT_EQ(expected_candidate_num, observer_.mline_0_candidates_.size());
    if (bundle) {
      EXPECT_EQ(0, observer_.mline_1_candidates_.size());
    } else {
      EXPECT_EQ(expected_candidate_num, observer_.mline_1_candidates_.size());
    }
  }
  // Tests that we can only send DTMF when the dtmf codec is supported.
  void TestCanInsertDtmf(bool can) {
    if (can) {
      InitWithDtmfCodec();
    } else {
      Init();
    }
    SendAudioVideoStream1();
    CreateAndSetRemoteOfferAndLocalAnswer();
    EXPECT_FALSE(session_->CanInsertDtmf(""));
    EXPECT_EQ(can, session_->CanInsertDtmf(kAudioTrack1));
  }

  // Helper class to configure loopback network and verify Best
  // Connection using right IP protocol for TestLoopbackCall
  // method. LoopbackNetworkManager applies firewall rules to block
  // all ping traffic once ICE completed, and remove them to observe
  // ICE reconnected again. This LoopbackNetworkConfiguration struct
  // verifies the best connection is using the right IP protocol after
  // initial ICE convergences.

  class LoopbackNetworkConfiguration {
   public:
    LoopbackNetworkConfiguration()
        : test_ipv6_network_(false),
          test_extra_ipv4_network_(false),
          best_connection_after_initial_ice_converged_(1, 0) {}

    // Used to track the expected best connection count in each IP protocol.
    struct ExpectedBestConnection {
      ExpectedBestConnection(int ipv4_count, int ipv6_count)
          : ipv4_count_(ipv4_count),
            ipv6_count_(ipv6_count) {}

      int ipv4_count_;
      int ipv6_count_;
    };

    bool test_ipv6_network_;
    bool test_extra_ipv4_network_;
    ExpectedBestConnection best_connection_after_initial_ice_converged_;

    void VerifyBestConnectionAfterIceConverge(
        const rtc::scoped_refptr<FakeMetricsObserver> metrics_observer) const {
      Verify(metrics_observer, best_connection_after_initial_ice_converged_);
    }

   private:
    void Verify(const rtc::scoped_refptr<FakeMetricsObserver> metrics_observer,
                const ExpectedBestConnection& expected) const {
      EXPECT_EQ(
          metrics_observer->GetEnumCounter(webrtc::kEnumCounterAddressFamily,
                                           webrtc::kBestConnections_IPv4),
          expected.ipv4_count_);
      EXPECT_EQ(
          metrics_observer->GetEnumCounter(webrtc::kEnumCounterAddressFamily,
                                           webrtc::kBestConnections_IPv6),
          expected.ipv6_count_);
      // This is used in the loopback call so there is only single host to host
      // candidate pair.
      EXPECT_EQ(metrics_observer->GetEnumCounter(
                    webrtc::kEnumCounterIceCandidatePairTypeUdp,
                    webrtc::kIceCandidatePairHostHost),
                0);
      EXPECT_EQ(metrics_observer->GetEnumCounter(
                    webrtc::kEnumCounterIceCandidatePairTypeUdp,
                    webrtc::kIceCandidatePairHostPublicHostPublic),
                1);
    }
  };

  class LoopbackNetworkManager {
   public:
    LoopbackNetworkManager(WebRtcSessionTest* session,
                           const LoopbackNetworkConfiguration& config)
        : config_(config) {
      session->AddInterface(
          rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
      if (config_.test_extra_ipv4_network_) {
        session->AddInterface(
            rtc::SocketAddress(kClientAddrHost2, kClientAddrPort));
      }
      if (config_.test_ipv6_network_) {
        session->AddInterface(
            rtc::SocketAddress(kClientIPv6AddrHost1, kClientAddrPort));
      }
    }

    void ApplyFirewallRules(rtc::FirewallSocketServer* fss) {
      fss->AddRule(false, rtc::FP_ANY, rtc::FD_ANY,
                   rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
      if (config_.test_extra_ipv4_network_) {
        fss->AddRule(false, rtc::FP_ANY, rtc::FD_ANY,
                     rtc::SocketAddress(kClientAddrHost2, kClientAddrPort));
      }
      if (config_.test_ipv6_network_) {
        fss->AddRule(false, rtc::FP_ANY, rtc::FD_ANY,
                     rtc::SocketAddress(kClientIPv6AddrHost1, kClientAddrPort));
      }
    }

    void ClearRules(rtc::FirewallSocketServer* fss) { fss->ClearRules(); }

   private:
    LoopbackNetworkConfiguration config_;
  };

  // The method sets up a call from the session to itself, in a loopback
  // arrangement.  It also uses a firewall rule to create a temporary
  // disconnection, and then a permanent disconnection.
  // This code is placed in a method so that it can be invoked
  // by multiple tests with different allocators (e.g. with and without BUNDLE).
  // While running the call, this method also checks if the session goes through
  // the correct sequence of ICE states when a connection is established,
  // broken, and re-established.
  // The Connection state should go:
  // New -> Checking -> (Connected) -> Completed -> Disconnected -> Completed
  //     -> Failed.
  // The Gathering state should go: New -> Gathering -> Completed.

  void SetupLoopbackCall() {
    Init();
    SendAudioVideoStream1();
    SessionDescriptionInterface* offer = CreateOffer();

    EXPECT_EQ(PeerConnectionInterface::kIceGatheringNew,
              observer_.ice_gathering_state_);
    SetLocalDescriptionWithoutError(offer);
    EXPECT_EQ(PeerConnectionInterface::kIceConnectionNew,
              observer_.ice_connection_state_);
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceGatheringGathering,
                   observer_.ice_gathering_state_, kIceCandidatesTimeout);
    EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceGatheringComplete,
                   observer_.ice_gathering_state_, kIceCandidatesTimeout);

    std::string sdp;
    offer->ToString(&sdp);
    SessionDescriptionInterface* desc = webrtc::CreateSessionDescription(
        JsepSessionDescription::kAnswer, sdp, nullptr);
    ASSERT_TRUE(desc != NULL);
    SetRemoteDescriptionWithoutError(desc);

    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionChecking,
                   observer_.ice_connection_state_, kIceCandidatesTimeout);

    // The ice connection state is "Connected" too briefly to catch in a test.
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionCompleted,
                   observer_.ice_connection_state_, kIceCandidatesTimeout);
  }

  void TestLoopbackCall(const LoopbackNetworkConfiguration& config) {
    LoopbackNetworkManager loopback_network_manager(this, config);
    SetupLoopbackCall();
    config.VerifyBestConnectionAfterIceConverge(metrics_observer_);
    // Adding firewall rule to block ping requests, which should cause
    // transport channel failure.

    loopback_network_manager.ApplyFirewallRules(fss_.get());

    LOG(LS_INFO) << "Firewall Rules applied";
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionDisconnected,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout);

    metrics_observer_->Reset();

    // Clearing the rules, session should move back to completed state.
    loopback_network_manager.ClearRules(fss_.get());

    LOG(LS_INFO) << "Firewall Rules cleared";
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionCompleted,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout);

    // Now we block ping requests and wait until the ICE connection transitions
    // to the Failed state.  This will take at least 30 seconds because it must
    // wait for the Port to timeout.
    int port_timeout = 30000;

    loopback_network_manager.ApplyFirewallRules(fss_.get());
    LOG(LS_INFO) << "Firewall Rules applied again";
    EXPECT_EQ_WAIT(PeerConnectionInterface::kIceConnectionDisconnected,
                   observer_.ice_connection_state_,
                   kIceCandidatesTimeout + port_timeout);
  }

  void TestLoopbackCall() {
    LoopbackNetworkConfiguration config;
    TestLoopbackCall(config);
  }

  void TestPacketOptions() {
    media_controller_.reset(
        new cricket::FakeMediaController(channel_manager_.get(), &fake_call_));
    LoopbackNetworkConfiguration config;
    LoopbackNetworkManager loopback_network_manager(this, config);

    SetupLoopbackCall();

    uint8_t test_packet[15] = {0};
    rtc::PacketOptions options;
    options.packet_id = 10;
    media_engine_->GetVideoChannel(0)
        ->SendRtp(test_packet, sizeof(test_packet), options);

    const int kPacketTimeout = 2000;
    EXPECT_EQ_WAIT(fake_call_.last_sent_packet().packet_id, 10, kPacketTimeout);
    EXPECT_GT(fake_call_.last_sent_packet().send_time_ms, -1);
  }

  // Adds CN codecs to FakeMediaEngine and MediaDescriptionFactory.
  void AddCNCodecs() {
    const cricket::AudioCodec kCNCodec1(102, "CN", 8000, 0, 1, 0);
    const cricket::AudioCodec kCNCodec2(103, "CN", 16000, 0, 1, 0);

    // Add kCNCodec for dtmf test.
    std::vector<cricket::AudioCodec> codecs = media_engine_->audio_codecs();;
    codecs.push_back(kCNCodec1);
    codecs.push_back(kCNCodec2);
    media_engine_->SetAudioCodecs(codecs);
    desc_factory_->set_audio_codecs(codecs);
  }

  bool VerifyNoCNCodecs(const cricket::ContentInfo* content) {
    const cricket::ContentDescription* description = content->description;
    ASSERT(description != NULL);
    const cricket::AudioContentDescription* audio_content_desc =
        static_cast<const cricket::AudioContentDescription*>(description);
    ASSERT(audio_content_desc != NULL);
    for (size_t i = 0; i < audio_content_desc->codecs().size(); ++i) {
      if (audio_content_desc->codecs()[i].name == "CN")
        return false;
    }
    return true;
  }

  void CreateDataChannel() {
    webrtc::InternalDataChannelInit dci;
    dci.reliable = session_->data_channel_type() == cricket::DCT_SCTP;
    data_channel_ = DataChannel::Create(
        session_.get(), session_->data_channel_type(), "datachannel", dci);
  }

  void SetLocalDescriptionWithDataChannel() {
    CreateDataChannel();
    SessionDescriptionInterface* offer = CreateOffer();
    SetLocalDescriptionWithoutError(offer);
  }

  void VerifyMultipleAsyncCreateDescription(
      RTCCertificateGenerationMethod cert_gen_method,
      CreateSessionDescriptionRequest::Type type) {
    InitWithDtls(cert_gen_method);
    VerifyMultipleAsyncCreateDescriptionAfterInit(true, type);
  }

  void VerifyMultipleAsyncCreateDescriptionIdentityGenFailure(
      CreateSessionDescriptionRequest::Type type) {
    InitWithDtlsIdentityGenFail();
    VerifyMultipleAsyncCreateDescriptionAfterInit(false, type);
  }

  void VerifyMultipleAsyncCreateDescriptionAfterInit(
      bool success, CreateSessionDescriptionRequest::Type type) {
    RTC_CHECK(session_);
    SetFactoryDtlsSrtp();
    if (type == CreateSessionDescriptionRequest::kAnswer) {
      cricket::MediaSessionOptions options;
      scoped_ptr<JsepSessionDescription> offer(
            CreateRemoteOffer(options, cricket::SEC_DISABLED));
      ASSERT_TRUE(offer.get() != NULL);
      SetRemoteDescriptionWithoutError(offer.release());
    }

    PeerConnectionInterface::RTCOfferAnswerOptions options;
    cricket::MediaSessionOptions session_options;
    const int kNumber = 3;
    rtc::scoped_refptr<WebRtcSessionCreateSDPObserverForTest>
        observers[kNumber];
    for (int i = 0; i < kNumber; ++i) {
      observers[i] = new WebRtcSessionCreateSDPObserverForTest();
      if (type == CreateSessionDescriptionRequest::kOffer) {
        session_->CreateOffer(observers[i], options, session_options);
      } else {
        session_->CreateAnswer(observers[i], nullptr, session_options);
      }
    }

    WebRtcSessionCreateSDPObserverForTest::State expected_state =
        success ? WebRtcSessionCreateSDPObserverForTest::kSucceeded :
                  WebRtcSessionCreateSDPObserverForTest::kFailed;

    for (int i = 0; i < kNumber; ++i) {
      EXPECT_EQ_WAIT(expected_state, observers[i]->state(), 1000);
      if (success) {
        EXPECT_TRUE(observers[i]->description() != NULL);
      } else {
        EXPECT_TRUE(observers[i]->description() == NULL);
      }
    }
  }

  void ConfigureAllocatorWithTurn() {
    cricket::RelayServerConfig turn_server(cricket::RELAY_TURN);
    cricket::RelayCredentials credentials(kTurnUsername, kTurnPassword);
    turn_server.credentials = credentials;
    turn_server.ports.push_back(
        cricket::ProtocolAddress(kTurnUdpIntAddr, cricket::PROTO_UDP, false));
    allocator_->AddTurnServer(turn_server);
    allocator_->set_step_delay(cricket::kMinimumStepDelay);
    allocator_->set_flags(cricket::PORTALLOCATOR_DISABLE_TCP);
  }

  cricket::FakeMediaEngine* media_engine_;
  cricket::FakeDataEngine* data_engine_;
  rtc::scoped_ptr<cricket::ChannelManager> channel_manager_;
  cricket::FakeCall fake_call_;
  rtc::scoped_ptr<webrtc::MediaControllerInterface> media_controller_;
  rtc::scoped_ptr<cricket::TransportDescriptionFactory> tdesc_factory_;
  rtc::scoped_ptr<cricket::MediaSessionDescriptionFactory> desc_factory_;
  rtc::scoped_ptr<rtc::PhysicalSocketServer> pss_;
  rtc::scoped_ptr<rtc::VirtualSocketServer> vss_;
  rtc::scoped_ptr<rtc::FirewallSocketServer> fss_;
  rtc::SocketServerScope ss_scope_;
  rtc::SocketAddress stun_socket_addr_;
  rtc::scoped_ptr<cricket::TestStunServer> stun_server_;
  cricket::TestTurnServer turn_server_;
  rtc::FakeNetworkManager network_manager_;
  rtc::scoped_ptr<cricket::BasicPortAllocator> allocator_;
  PeerConnectionFactoryInterface::Options options_;
  rtc::scoped_ptr<FakeConstraints> constraints_;
  rtc::scoped_ptr<WebRtcSessionForTest> session_;
  MockIceObserver observer_;
  cricket::FakeVideoMediaChannel* video_channel_;
  cricket::FakeVoiceMediaChannel* voice_channel_;
  rtc::scoped_refptr<FakeMetricsObserver> metrics_observer_;
  // The following flags affect options created for CreateOffer/CreateAnswer.
  bool send_stream_1_ = false;
  bool send_stream_2_ = false;
  bool send_audio_ = false;
  bool send_video_ = false;
  rtc::scoped_refptr<DataChannel> data_channel_;
  // Last values received from data channel creation signal.
  std::string last_data_channel_label_;
  InternalDataChannelInit last_data_channel_config_;
};

TEST_P(WebRtcSessionTest, TestInitializeWithDtls) {
  InitWithDtls(GetParam());
  // SDES is disabled when DTLS is on.
  EXPECT_EQ(cricket::SEC_DISABLED, session_->SdesPolicy());
}

TEST_F(WebRtcSessionTest, TestInitializeWithoutDtls) {
  Init();
  // SDES is required if DTLS is off.
  EXPECT_EQ(cricket::SEC_REQUIRED, session_->SdesPolicy());
}

TEST_F(WebRtcSessionTest, TestSessionCandidates) {
  TestSessionCandidatesWithBundleRtcpMux(false, false);
}

// Below test cases (TestSessionCandidatesWith*) verify the candidates gathered
// with rtcp-mux and/or bundle.
TEST_F(WebRtcSessionTest, TestSessionCandidatesWithRtcpMux) {
  TestSessionCandidatesWithBundleRtcpMux(false, true);
}

TEST_F(WebRtcSessionTest, TestSessionCandidatesWithBundleRtcpMux) {
  TestSessionCandidatesWithBundleRtcpMux(true, true);
}

TEST_F(WebRtcSessionTest, TestMultihomeCandidates) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  AddInterface(rtc::SocketAddress(kClientAddrHost2, kClientAddrPort));
  Init();
  SendAudioVideoStream1();
  InitiateCall();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(8u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(8u, observer_.mline_1_candidates_.size());
}

TEST_F(WebRtcSessionTest, TestStunError) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  AddInterface(rtc::SocketAddress(kClientAddrHost2, kClientAddrPort));
  fss_->AddRule(false,
                rtc::FP_UDP,
                rtc::FD_ANY,
                rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  Init();
  SendAudioVideoStream1();
  InitiateCall();
  // Since kClientAddrHost1 is blocked, not expecting stun candidates for it.
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(6u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(6u, observer_.mline_1_candidates_.size());
}

// Test session delivers no candidates gathered when constraint set to "none".
TEST_F(WebRtcSessionTest, TestIceTransportsNone) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  InitWithIceTransport(PeerConnectionInterface::kNone);
  SendAudioVideoStream1();
  InitiateCall();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(0u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(0u, observer_.mline_1_candidates_.size());
}

// Test session delivers only relay candidates gathered when constaint set to
// "relay".
TEST_F(WebRtcSessionTest, TestIceTransportsRelay) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  ConfigureAllocatorWithTurn();
  InitWithIceTransport(PeerConnectionInterface::kRelay);
  SendAudioVideoStream1();
  InitiateCall();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  EXPECT_EQ(2u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(2u, observer_.mline_1_candidates_.size());
  for (size_t i = 0; i < observer_.mline_0_candidates_.size(); ++i) {
    EXPECT_EQ(cricket::RELAY_PORT_TYPE,
              observer_.mline_0_candidates_[i].type());
  }
  for (size_t i = 0; i < observer_.mline_1_candidates_.size(); ++i) {
    EXPECT_EQ(cricket::RELAY_PORT_TYPE,
              observer_.mline_1_candidates_[i].type());
  }
}

// Test session delivers all candidates gathered when constaint set to "all".
TEST_F(WebRtcSessionTest, TestIceTransportsAll) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  InitWithIceTransport(PeerConnectionInterface::kAll);
  SendAudioVideoStream1();
  InitiateCall();
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
  // Host + STUN. By default allocator is disabled to gather relay candidates.
  EXPECT_EQ(4u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(4u, observer_.mline_1_candidates_.size());
}

TEST_F(WebRtcSessionTest, SetSdpFailedOnInvalidSdp) {
  Init();
  SessionDescriptionInterface* offer = NULL;
  // Since |offer| is NULL, there's no way to tell if it's an offer or answer.
  std::string unknown_action;
  SetLocalDescriptionExpectError(unknown_action, kInvalidSdp, offer);
  SetRemoteDescriptionExpectError(unknown_action, kInvalidSdp, offer);
}

// Test creating offers and receive answers and make sure the
// media engine creates the expected send and receive streams.
TEST_F(WebRtcSessionTest, TestCreateSdesOfferReceiveSdesAnswer) {
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();
  const std::string session_id_orig = offer->session_id();
  const std::string session_version_orig = offer->session_version();
  SetLocalDescriptionWithoutError(offer);

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].id);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].id);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].id);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].id);

  // Create new offer without send streams.
  SendNothing();
  offer = CreateOffer();

  // Verify the session id is the same and the session version is
  // increased.
  EXPECT_EQ(session_id_orig, offer->session_id());
  EXPECT_LT(rtc::FromString<uint64_t>(session_version_orig),
            rtc::FromString<uint64_t>(offer->session_version()));

  SetLocalDescriptionWithoutError(offer);
  EXPECT_EQ(0u, video_channel_->send_streams().size());
  EXPECT_EQ(0u, voice_channel_->send_streams().size());

  SendAudioVideoStream2();
  answer = CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  // Make sure the receive streams have not changed.
  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].id);
  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].id);
}

// Test receiving offers and creating answers and make sure the
// media engine creates the expected send and receive streams.
TEST_F(WebRtcSessionTest, TestReceiveSdesOfferCreateSdesAnswer) {
  Init();
  SendAudioVideoStream2();
  SessionDescriptionInterface* offer = CreateOffer();
  VerifyCryptoParams(offer->description());
  SetRemoteDescriptionWithoutError(offer);

  SendAudioVideoStream1();
  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  VerifyCryptoParams(answer->description());
  SetLocalDescriptionWithoutError(answer);

  const std::string session_id_orig = answer->session_id();
  const std::string session_version_orig = answer->session_version();

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[0].id);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[0].id);

  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->send_streams()[0].id);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->send_streams()[0].id);

  SendAudioVideoStream1And2();
  offer = CreateOffer();
  SetRemoteDescriptionWithoutError(offer);

  // Answer by turning off all send streams.
  SendNothing();
  answer = CreateAnswer(NULL);

  // Verify the session id is the same and the session version is
  // increased.
  EXPECT_EQ(session_id_orig, answer->session_id());
  EXPECT_LT(rtc::FromString<uint64_t>(session_version_orig),
            rtc::FromString<uint64_t>(answer->session_version()));
  SetLocalDescriptionWithoutError(answer);

  ASSERT_EQ(2u, video_channel_->recv_streams().size());
  EXPECT_TRUE(kVideoTrack1 == video_channel_->recv_streams()[0].id);
  EXPECT_TRUE(kVideoTrack2 == video_channel_->recv_streams()[1].id);
  ASSERT_EQ(2u, voice_channel_->recv_streams().size());
  EXPECT_TRUE(kAudioTrack1 == voice_channel_->recv_streams()[0].id);
  EXPECT_TRUE(kAudioTrack2 == voice_channel_->recv_streams()[1].id);

  // Make sure we have no send streams.
  EXPECT_EQ(0u, video_channel_->send_streams().size());
  EXPECT_EQ(0u, voice_channel_->send_streams().size());
}

TEST_F(WebRtcSessionTest, SetLocalSdpFailedOnCreateChannel) {
  Init();
  media_engine_->set_fail_create_channel(true);

  SessionDescriptionInterface* offer = CreateOffer();
  ASSERT_TRUE(offer != NULL);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetRemoteDescriptionOfferExpectError(kCreateChannelFailed, offer);
  offer = CreateOffer();
  ASSERT_TRUE(offer != NULL);
  SetLocalDescriptionOfferExpectError(kCreateChannelFailed, offer);
}

//
// Tests for creating/setting SDP under different SDES/DTLS polices:
//
// --DTLS off and SDES on
// TestCreateSdesOfferReceiveSdesAnswer/TestReceiveSdesOfferCreateSdesAnswer:
//     set local/remote offer/answer with crypto --> success
// TestSetNonSdesOfferWhenSdesOn: set local/remote offer without crypto --->
//     failure
// TestSetLocalNonSdesAnswerWhenSdesOn: set local answer without crypto -->
//     failure
// TestSetRemoteNonSdesAnswerWhenSdesOn: set remote answer without crypto -->
//     failure
//
// --DTLS on and SDES off
// TestCreateDtlsOfferReceiveDtlsAnswer/TestReceiveDtlsOfferCreateDtlsAnswer:
//     set local/remote offer/answer with DTLS fingerprint --> success
// TestReceiveNonDtlsOfferWhenDtlsOn: set local/remote offer without DTLS
//     fingerprint --> failure
// TestSetLocalNonDtlsAnswerWhenDtlsOn: set local answer without fingerprint
//     --> failure
// TestSetRemoteNonDtlsAnswerWhenDtlsOn: set remote answer without fingerprint
//     --> failure
//
// --Encryption disabled: DTLS off and SDES off
// TestCreateOfferReceiveAnswerWithoutEncryption: set local offer and remote
//     answer without SDES or DTLS --> success
// TestCreateAnswerReceiveOfferWithoutEncryption: set remote offer and local
//     answer without SDES or DTLS --> success
//

// Test that we return a failure when applying a remote/local offer that doesn't
// have cryptos enabled when DTLS is off.
TEST_F(WebRtcSessionTest, TestSetNonSdesOfferWhenSdesOn) {
  Init();
  cricket::MediaSessionOptions options;
  options.recv_video = true;
  JsepSessionDescription* offer = CreateRemoteOffer(
      options, cricket::SEC_DISABLED);
  ASSERT_TRUE(offer != NULL);
  VerifyNoCryptoParams(offer->description(), false);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetRemoteDescriptionOfferExpectError(kSdpWithoutSdesCrypto, offer);
  offer = CreateRemoteOffer(options, cricket::SEC_DISABLED);
  ASSERT_TRUE(offer != NULL);
  SetLocalDescriptionOfferExpectError(kSdpWithoutSdesCrypto, offer);
}

// Test that we return a failure when applying a local answer that doesn't have
// cryptos enabled when DTLS is off.
TEST_F(WebRtcSessionTest, TestSetLocalNonSdesAnswerWhenSdesOn) {
  Init();
  SessionDescriptionInterface* offer = NULL;
  SessionDescriptionInterface* answer = NULL;
  CreateCryptoOfferAndNonCryptoAnswer(&offer, &answer);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetRemoteDescriptionWithoutError(offer);
  SetLocalDescriptionAnswerExpectError(kSdpWithoutSdesCrypto, answer);
}

// Test we will return fail when apply an remote answer that doesn't have
// crypto enabled when DTLS is off.
TEST_F(WebRtcSessionTest, TestSetRemoteNonSdesAnswerWhenSdesOn) {
  Init();
  SessionDescriptionInterface* offer = NULL;
  SessionDescriptionInterface* answer = NULL;
  CreateCryptoOfferAndNonCryptoAnswer(&offer, &answer);
  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionAnswerExpectError(kSdpWithoutSdesCrypto, answer);
}

// Test that we accept an offer with a DTLS fingerprint when DTLS is on
// and that we return an answer with a DTLS fingerprint.
TEST_P(WebRtcSessionTest, TestReceiveDtlsOfferCreateDtlsAnswer) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  SendAudioVideoStream1();
  InitWithDtls(GetParam());
  SetFactoryDtlsSrtp();
  cricket::MediaSessionOptions options;
  options.recv_video = true;
  JsepSessionDescription* offer =
      CreateRemoteOffer(options, cricket::SEC_DISABLED);
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), true);
  VerifyNoCryptoParams(offer->description(), true);

  // SetRemoteDescription will take the ownership of the offer.
  SetRemoteDescriptionWithoutError(offer);

  // Verify that we get a crypto fingerprint in the answer.
  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  ASSERT_TRUE(answer != NULL);
  VerifyFingerprintStatus(answer->description(), true);
  // Check that we don't have an a=crypto line in the answer.
  VerifyNoCryptoParams(answer->description(), true);

  // Now set the local description, which should work, even without a=crypto.
  SetLocalDescriptionWithoutError(answer);
}

// Test that we set a local offer with a DTLS fingerprint when DTLS is on
// and then we accept a remote answer with a DTLS fingerprint successfully.
TEST_P(WebRtcSessionTest, TestCreateDtlsOfferReceiveDtlsAnswer) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  SendAudioVideoStream1();
  InitWithDtls(GetParam());
  SetFactoryDtlsSrtp();

  // Verify that we get a crypto fingerprint in the answer.
  SessionDescriptionInterface* offer = CreateOffer();
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), true);
  // Check that we don't have an a=crypto line in the offer.
  VerifyNoCryptoParams(offer->description(), true);

  // Now set the local description, which should work, even without a=crypto.
  SetLocalDescriptionWithoutError(offer);

  cricket::MediaSessionOptions options;
  options.recv_video = true;
  JsepSessionDescription* answer =
      CreateRemoteAnswer(offer, options, cricket::SEC_DISABLED);
  ASSERT_TRUE(answer != NULL);
  VerifyFingerprintStatus(answer->description(), true);
  VerifyNoCryptoParams(answer->description(), true);

  // SetRemoteDescription will take the ownership of the answer.
  SetRemoteDescriptionWithoutError(answer);
}

// Test that if we support DTLS and the other side didn't offer a fingerprint,
// we will fail to set the remote description.
TEST_P(WebRtcSessionTest, TestReceiveNonDtlsOfferWhenDtlsOn) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());
  cricket::MediaSessionOptions options;
  options.recv_video = true;
  options.bundle_enabled = true;
  JsepSessionDescription* offer = CreateRemoteOffer(
      options, cricket::SEC_REQUIRED);
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), false);
  VerifyCryptoParams(offer->description());

  // SetRemoteDescription will take the ownership of the offer.
  SetRemoteDescriptionOfferExpectError(
      kSdpWithoutDtlsFingerprint, offer);

  offer = CreateRemoteOffer(options, cricket::SEC_REQUIRED);
  // SetLocalDescription will take the ownership of the offer.
  SetLocalDescriptionOfferExpectError(
      kSdpWithoutDtlsFingerprint, offer);
}

// Test that we return a failure when applying a local answer that doesn't have
// a DTLS fingerprint when DTLS is required.
TEST_P(WebRtcSessionTest, TestSetLocalNonDtlsAnswerWhenDtlsOn) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());
  SessionDescriptionInterface* offer = NULL;
  SessionDescriptionInterface* answer = NULL;
  CreateDtlsOfferAndNonDtlsAnswer(&offer, &answer);

  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer and answer.
  SetRemoteDescriptionWithoutError(offer);
  SetLocalDescriptionAnswerExpectError(
      kSdpWithoutDtlsFingerprint, answer);
}

// Test that we return a failure when applying a remote answer that doesn't have
// a DTLS fingerprint when DTLS is required.
TEST_P(WebRtcSessionTest, TestSetRemoteNonDtlsAnswerWhenDtlsOn) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());
  SessionDescriptionInterface* offer = CreateOffer();
  cricket::MediaSessionOptions options;
  options.recv_video = true;
  rtc::scoped_ptr<SessionDescriptionInterface> temp_offer(
      CreateRemoteOffer(options, cricket::SEC_ENABLED));
  JsepSessionDescription* answer =
      CreateRemoteAnswer(temp_offer.get(), options, cricket::SEC_ENABLED);

  // SetRemoteDescription and SetLocalDescription will take the ownership of
  // the offer and answer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionAnswerExpectError(
      kSdpWithoutDtlsFingerprint, answer);
}

// Test that we create a local offer without SDES or DTLS and accept a remote
// answer without SDES or DTLS when encryption is disabled.
TEST_P(WebRtcSessionTest, TestCreateOfferReceiveAnswerWithoutEncryption) {
  SendAudioVideoStream1();
  options_.disable_encryption = true;
  InitWithDtls(GetParam());

  // Verify that we get a crypto fingerprint in the answer.
  SessionDescriptionInterface* offer = CreateOffer();
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), false);
  // Check that we don't have an a=crypto line in the offer.
  VerifyNoCryptoParams(offer->description(), false);

  // Now set the local description, which should work, even without a=crypto.
  SetLocalDescriptionWithoutError(offer);

  cricket::MediaSessionOptions options;
  options.recv_video = true;
  JsepSessionDescription* answer =
      CreateRemoteAnswer(offer, options, cricket::SEC_DISABLED);
  ASSERT_TRUE(answer != NULL);
  VerifyFingerprintStatus(answer->description(), false);
  VerifyNoCryptoParams(answer->description(), false);

  // SetRemoteDescription will take the ownership of the answer.
  SetRemoteDescriptionWithoutError(answer);
}

// Test that we create a local answer without SDES or DTLS and accept a remote
// offer without SDES or DTLS when encryption is disabled.
TEST_P(WebRtcSessionTest, TestCreateAnswerReceiveOfferWithoutEncryption) {
  options_.disable_encryption = true;
  InitWithDtls(GetParam());

  cricket::MediaSessionOptions options;
  options.recv_video = true;
  JsepSessionDescription* offer =
      CreateRemoteOffer(options, cricket::SEC_DISABLED);
  ASSERT_TRUE(offer != NULL);
  VerifyFingerprintStatus(offer->description(), false);
  VerifyNoCryptoParams(offer->description(), false);

  // SetRemoteDescription will take the ownership of the offer.
  SetRemoteDescriptionWithoutError(offer);

  // Verify that we get a crypto fingerprint in the answer.
  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  ASSERT_TRUE(answer != NULL);
  VerifyFingerprintStatus(answer->description(), false);
  // Check that we don't have an a=crypto line in the answer.
  VerifyNoCryptoParams(answer->description(), false);

  // Now set the local description, which should work, even without a=crypto.
  SetLocalDescriptionWithoutError(answer);
}

TEST_F(WebRtcSessionTest, TestSetLocalOfferTwice) {
  Init();
  SendNothing();
  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);

  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer2 = CreateOffer();
  SetLocalDescriptionWithoutError(offer2);
}

TEST_F(WebRtcSessionTest, TestSetRemoteOfferTwice) {
  Init();
  SendNothing();
  // SetLocalDescription take ownership of offer.
  SessionDescriptionInterface* offer = CreateOffer();
  SetRemoteDescriptionWithoutError(offer);

  SessionDescriptionInterface* offer2 = CreateOffer();
  SetRemoteDescriptionWithoutError(offer2);
}

TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteOffer) {
  Init();
  SendNothing();
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);
  offer = CreateOffer();
  SetRemoteDescriptionOfferExpectError("Called in wrong state: STATE_SENTOFFER",
                                       offer);
}

TEST_F(WebRtcSessionTest, TestSetRemoteAndLocalOffer) {
  Init();
  SendNothing();
  SessionDescriptionInterface* offer = CreateOffer();
  SetRemoteDescriptionWithoutError(offer);
  offer = CreateOffer();
  SetLocalDescriptionOfferExpectError(
      "Called in wrong state: STATE_RECEIVEDOFFER", offer);
}

TEST_F(WebRtcSessionTest, TestSetLocalPrAnswer) {
  Init();
  SendNothing();
  SessionDescriptionInterface* offer = CreateRemoteOffer();
  SetRemoteDescriptionExpectState(offer, WebRtcSession::STATE_RECEIVEDOFFER);

  JsepSessionDescription* pranswer = static_cast<JsepSessionDescription*>(
      CreateAnswer(NULL));
  pranswer->set_type(SessionDescriptionInterface::kPrAnswer);
  SetLocalDescriptionExpectState(pranswer, WebRtcSession::STATE_SENTPRANSWER);

  SendAudioVideoStream1();
  JsepSessionDescription* pranswer2 = static_cast<JsepSessionDescription*>(
      CreateAnswer(NULL));
  pranswer2->set_type(SessionDescriptionInterface::kPrAnswer);

  SetLocalDescriptionExpectState(pranswer2, WebRtcSession::STATE_SENTPRANSWER);

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  SetLocalDescriptionExpectState(answer, WebRtcSession::STATE_INPROGRESS);
}

TEST_F(WebRtcSessionTest, TestSetRemotePrAnswer) {
  Init();
  SendNothing();
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionExpectState(offer, WebRtcSession::STATE_SENTOFFER);

  JsepSessionDescription* pranswer =
      CreateRemoteAnswer(session_->local_description());
  pranswer->set_type(SessionDescriptionInterface::kPrAnswer);

  SetRemoteDescriptionExpectState(pranswer,
                                  WebRtcSession::STATE_RECEIVEDPRANSWER);

  SendAudioVideoStream1();
  JsepSessionDescription* pranswer2 =
      CreateRemoteAnswer(session_->local_description());
  pranswer2->set_type(SessionDescriptionInterface::kPrAnswer);

  SetRemoteDescriptionExpectState(pranswer2,
                                  WebRtcSession::STATE_RECEIVEDPRANSWER);

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionExpectState(answer, WebRtcSession::STATE_INPROGRESS);
}

TEST_F(WebRtcSessionTest, TestSetLocalAnswerWithoutOffer) {
  Init();
  SendNothing();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(offer.get());
  SetLocalDescriptionAnswerExpectError("Called in wrong state: STATE_INIT",
                                       answer);
}

TEST_F(WebRtcSessionTest, TestSetRemoteAnswerWithoutOffer) {
  Init();
  SendNothing();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(offer.get());
  SetRemoteDescriptionAnswerExpectError(
      "Called in wrong state: STATE_INIT", answer);
}

TEST_F(WebRtcSessionTest, TestAddRemoteCandidate) {
  Init();
  SendAudioVideoStream1();

  cricket::Candidate candidate;
  candidate.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName0, 0, candidate);

  // Fail since we have not set a remote description.
  EXPECT_FALSE(session_->ProcessIceMessage(&ice_candidate1));

  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);

  // Fail since we have not set a remote description.
  EXPECT_FALSE(session_->ProcessIceMessage(&ice_candidate1));

  SessionDescriptionInterface* answer = CreateRemoteAnswer(
      session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate1));
  candidate.set_component(2);
  JsepIceCandidate ice_candidate2(kMediaContentName0, 0, candidate);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate2));

  // Verifying the candidates are copied properly from internal vector.
  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(2u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());
  EXPECT_EQ(kMediaContentName0, candidates->at(0)->sdp_mid());
  EXPECT_EQ(1, candidates->at(0)->candidate().component());
  EXPECT_EQ(2, candidates->at(1)->candidate().component());

  // |ice_candidate3| is identical to |ice_candidate2|.  It can be added
  // successfully, but the total count of candidates will not increase.
  candidate.set_component(2);
  JsepIceCandidate ice_candidate3(kMediaContentName0, 0, candidate);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate3));
  ASSERT_EQ(2u, candidates->count());

  JsepIceCandidate bad_ice_candidate("bad content name", 99, candidate);
  EXPECT_FALSE(session_->ProcessIceMessage(&bad_ice_candidate));
}

// Test that a remote candidate is added to the remote session description and
// that it is retained if the remote session description is changed.
TEST_F(WebRtcSessionTest, TestRemoteCandidatesAddedToSessionDescription) {
  Init();
  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName0, kMediaContentIndex0,
                                  candidate1);
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();

  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate1));
  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(1u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());

  // Update the RemoteSessionDescription with a new session description and
  // a candidate and check that the new remote session description contains both
  // candidates.
  SessionDescriptionInterface* offer = CreateRemoteOffer();
  cricket::Candidate candidate2;
  JsepIceCandidate ice_candidate2(kMediaContentName0, kMediaContentIndex0,
                                  candidate2);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate2));
  SetRemoteDescriptionWithoutError(offer);

  remote_desc = session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  candidates = remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(2u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());
  // Username and password have be updated with the TransportInfo of the
  // SessionDescription, won't be equal to the original one.
  candidate2.set_username(candidates->at(0)->candidate().username());
  candidate2.set_password(candidates->at(0)->candidate().password());
  EXPECT_TRUE(candidate2.IsEquivalent(candidates->at(0)->candidate()));
  EXPECT_EQ(kMediaContentIndex0, candidates->at(1)->sdp_mline_index());
  // No need to verify the username and password.
  candidate1.set_username(candidates->at(1)->candidate().username());
  candidate1.set_password(candidates->at(1)->candidate().password());
  EXPECT_TRUE(candidate1.IsEquivalent(candidates->at(1)->candidate()));

  // Test that the candidate is ignored if we can add the same candidate again.
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate2));
}

// Test that local candidates are added to the local session description and
// that they are retained if the local session description is changed.
TEST_F(WebRtcSessionTest, TestLocalCandidatesAddedToSessionDescription) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();

  const SessionDescriptionInterface* local_desc = session_->local_description();
  const IceCandidateCollection* candidates =
      local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_EQ(0u, candidates->count());

  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);

  local_desc = session_->local_description();
  candidates = local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
  candidates = local_desc->candidates(1);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_EQ(0u, candidates->count());

  // Update the session descriptions.
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();

  local_desc = session_->local_description();
  candidates = local_desc->candidates(kMediaContentIndex0);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_LT(0u, candidates->count());
  candidates = local_desc->candidates(1);
  ASSERT_TRUE(candidates != NULL);
  EXPECT_EQ(0u, candidates->count());
}

// Test that we can set a remote session description with remote candidates.
TEST_F(WebRtcSessionTest, TestSetRemoteSessionDescriptionWithCandidates) {
  Init();

  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName0, kMediaContentIndex0,
                                 candidate1);
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();

  EXPECT_TRUE(offer->AddCandidate(&ice_candidate));
  SetRemoteDescriptionWithoutError(offer);

  const SessionDescriptionInterface* remote_desc =
      session_->remote_description();
  ASSERT_TRUE(remote_desc != NULL);
  ASSERT_EQ(2u, remote_desc->number_of_mediasections());
  const IceCandidateCollection* candidates =
      remote_desc->candidates(kMediaContentIndex0);
  ASSERT_EQ(1u, candidates->count());
  EXPECT_EQ(kMediaContentIndex0, candidates->at(0)->sdp_mline_index());

  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  SetLocalDescriptionWithoutError(answer);
}

// Test that offers and answers contains ice candidates when Ice candidates have
// been gathered.
TEST_F(WebRtcSessionTest, TestSetLocalAndRemoteDescriptionWithCandidates) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));
  Init();
  SendAudioVideoStream1();
  // Ice is started but candidates are not provided until SetLocalDescription
  // is called.
  EXPECT_EQ(0u, observer_.mline_0_candidates_.size());
  EXPECT_EQ(0u, observer_.mline_1_candidates_.size());
  CreateAndSetRemoteOfferAndLocalAnswer();
  // Wait until at least one local candidate has been collected.
  EXPECT_TRUE_WAIT(0u < observer_.mline_0_candidates_.size(),
                   kIceCandidatesTimeout);

  rtc::scoped_ptr<SessionDescriptionInterface> local_offer(CreateOffer());

  ASSERT_TRUE(local_offer->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, local_offer->candidates(kMediaContentIndex0)->count());

  SessionDescriptionInterface* remote_offer(CreateRemoteOffer());
  SetRemoteDescriptionWithoutError(remote_offer);
  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  ASSERT_TRUE(answer->candidates(kMediaContentIndex0) != NULL);
  EXPECT_LT(0u, answer->candidates(kMediaContentIndex0)->count());
  SetLocalDescriptionWithoutError(answer);
}

// Verifies TransportProxy and media channels are created with content names
// present in the SessionDescription.
TEST_F(WebRtcSessionTest, TestChannelCreationsWithContentNames) {
  Init();
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  // CreateOffer creates session description with the content names "audio" and
  // "video". Goal is to modify these content names and verify transport
  // channels
  // in the WebRtcSession, as channels are created with the content names
  // present in SDP.
  std::string sdp;
  EXPECT_TRUE(offer->ToString(&sdp));
  const std::string kAudioMid = "a=mid:audio";
  const std::string kAudioMidReplaceStr = "a=mid:audio_content_name";
  const std::string kVideoMid = "a=mid:video";
  const std::string kVideoMidReplaceStr = "a=mid:video_content_name";

  // Replacing |audio| with |audio_content_name|.
  rtc::replace_substrs(kAudioMid.c_str(), kAudioMid.length(),
                             kAudioMidReplaceStr.c_str(),
                             kAudioMidReplaceStr.length(),
                             &sdp);
  // Replacing |video| with |video_content_name|.
  rtc::replace_substrs(kVideoMid.c_str(), kVideoMid.length(),
                             kVideoMidReplaceStr.c_str(),
                             kVideoMidReplaceStr.length(),
                             &sdp);

  SessionDescriptionInterface* modified_offer =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);

  SetRemoteDescriptionWithoutError(modified_offer);

  SessionDescriptionInterface* answer =
      CreateAnswer(NULL);
  SetLocalDescriptionWithoutError(answer);

  cricket::TransportChannel* voice_transport_channel =
      session_->voice_rtp_transport_channel();
  EXPECT_TRUE(voice_transport_channel != NULL);
  EXPECT_EQ(voice_transport_channel->transport_name(), "audio_content_name");
  cricket::TransportChannel* video_transport_channel =
      session_->video_rtp_transport_channel();
  EXPECT_TRUE(video_transport_channel != NULL);
  EXPECT_EQ(video_transport_channel->transport_name(), "video_content_name");
  EXPECT_TRUE((video_channel_ = media_engine_->GetVideoChannel(0)) != NULL);
  EXPECT_TRUE((voice_channel_ = media_engine_->GetVoiceChannel(0)) != NULL);
}

// Test that an offer contains the correct media content descriptions based on
// the send streams when no constraints have been set.
TEST_F(WebRtcSessionTest, CreateOfferWithoutConstraintsOrStreams) {
  Init();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  ASSERT_TRUE(offer != NULL);
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an offer contains the correct media content descriptions based on
// the send streams when no constraints have been set.
TEST_F(WebRtcSessionTest, CreateOfferWithoutConstraints) {
  Init();
  // Test Audio only offer.
  SendAudioOnlyStream2();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);

  // Test Audio / Video offer.
  SendAudioVideoStream1();
  offer.reset(CreateOffer());
  content = cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content != NULL);
}

// Test that an offer contains no media content descriptions if
// kOfferToReceiveVideo and kOfferToReceiveAudio constraints are set to false.
TEST_F(WebRtcSessionTest, CreateOfferWithConstraintsWithoutStreams) {
  Init();
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 0;
  options.offer_to_receive_video = 0;

  rtc::scoped_ptr<SessionDescriptionInterface> offer(
      CreateOffer(options));

  ASSERT_TRUE(offer != NULL);
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content == NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an offer contains only audio media content descriptions if
// kOfferToReceiveAudio constraints are set to true.
TEST_F(WebRtcSessionTest, CreateAudioOnlyOfferWithConstraints) {
  Init();
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;

  rtc::scoped_ptr<SessionDescriptionInterface> offer(
        CreateOffer(options));

  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an offer contains audio and video media content descriptions if
// kOfferToReceiveAudio and kOfferToReceiveVideo constraints are set to true.
TEST_F(WebRtcSessionTest, CreateOfferWithConstraints) {
  Init();
  // Test Audio / Video offer.
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
  options.offer_to_receive_video =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;

  rtc::scoped_ptr<SessionDescriptionInterface> offer(
      CreateOffer(options));

  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);

  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content != NULL);

  // Sets constraints to false and verifies that audio/video contents are
  // removed.
  options.offer_to_receive_audio = 0;
  options.offer_to_receive_video = 0;
  offer.reset(CreateOffer(options));

  content = cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content == NULL);
  content = cricket::GetFirstVideoContent(offer->description());
  EXPECT_TRUE(content == NULL);
}

// Test that an answer can not be created if the last remote description is not
// an offer.
TEST_F(WebRtcSessionTest, CreateAnswerWithoutAnOffer) {
  Init();
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer = CreateRemoteAnswer(offer);
  SetRemoteDescriptionWithoutError(answer);
  EXPECT_TRUE(CreateAnswer(NULL) == NULL);
}

// Test that an answer contains the correct media content descriptions when no
// constraints have been set.
TEST_F(WebRtcSessionTest, CreateAnswerWithoutConstraintsOrStreams) {
  Init();
  // Create a remote offer with audio and video content.
  rtc::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());
  SetRemoteDescriptionWithoutError(offer.release());
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(NULL));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
}

// Test that an answer contains the correct media content descriptions when no
// constraints have been set and the offer only contain audio.
TEST_F(WebRtcSessionTest, CreateAudioAnswerWithoutConstraintsOrStreams) {
  Init();
  // Create a remote offer with audio only.
  cricket::MediaSessionOptions options;

  rtc::scoped_ptr<JsepSessionDescription> offer(
      CreateRemoteOffer(options));
  ASSERT_TRUE(cricket::GetFirstVideoContent(offer->description()) == NULL);
  ASSERT_TRUE(cricket::GetFirstAudioContent(offer->description()) != NULL);

  SetRemoteDescriptionWithoutError(offer.release());
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(NULL));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  EXPECT_TRUE(cricket::GetFirstVideoContent(answer->description()) == NULL);
}

// Test that an answer contains the correct media content descriptions when no
// constraints have been set.
TEST_F(WebRtcSessionTest, CreateAnswerWithoutConstraints) {
  Init();
  // Create a remote offer with audio and video content.
  rtc::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());
  SetRemoteDescriptionWithoutError(offer.release());
  // Test with a stream with tracks.
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(NULL));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
}

// Test that an answer contains the correct media content descriptions when
// constraints have been set but no stream is sent.
TEST_F(WebRtcSessionTest, CreateAnswerWithConstraintsWithoutStreams) {
  Init();
  // Create a remote offer with audio and video content.
  rtc::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());
  SetRemoteDescriptionWithoutError(offer.release());

  webrtc::FakeConstraints constraints_no_receive;
  constraints_no_receive.SetMandatoryReceiveAudio(false);
  constraints_no_receive.SetMandatoryReceiveVideo(false);

  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(&constraints_no_receive));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_TRUE(content->rejected);

  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_TRUE(content->rejected);
}

// Test that an answer contains the correct media content descriptions when
// constraints have been set and streams are sent.
TEST_F(WebRtcSessionTest, CreateAnswerWithConstraints) {
  Init();
  // Create a remote offer with audio and video content.
  rtc::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());
  SetRemoteDescriptionWithoutError(offer.release());

  webrtc::FakeConstraints constraints_no_receive;
  constraints_no_receive.SetMandatoryReceiveAudio(false);
  constraints_no_receive.SetMandatoryReceiveVideo(false);

  // Test with a stream with tracks.
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(&constraints_no_receive));

  // TODO(perkj): Should the direction be set to SEND_ONLY?
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);

  // TODO(perkj): Should the direction be set to SEND_ONLY?
  content = cricket::GetFirstVideoContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_FALSE(content->rejected);
}

TEST_F(WebRtcSessionTest, CreateOfferWithoutCNCodecs) {
  AddCNCodecs();
  Init();
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
  options.voice_activity_detection = false;

  rtc::scoped_ptr<SessionDescriptionInterface> offer(
      CreateOffer(options));

  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(offer->description());
  EXPECT_TRUE(content != NULL);
  EXPECT_TRUE(VerifyNoCNCodecs(content));
}

TEST_F(WebRtcSessionTest, CreateAnswerWithoutCNCodecs) {
  AddCNCodecs();
  Init();
  // Create a remote offer with audio and video content.
  rtc::scoped_ptr<JsepSessionDescription> offer(CreateRemoteOffer());
  SetRemoteDescriptionWithoutError(offer.release());

  webrtc::FakeConstraints constraints;
  constraints.SetOptionalVAD(false);
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(&constraints));
  const cricket::ContentInfo* content =
      cricket::GetFirstAudioContent(answer->description());
  ASSERT_TRUE(content != NULL);
  EXPECT_TRUE(VerifyNoCNCodecs(content));
}

// This test verifies the call setup when remote answer with audio only and
// later updates with video.
TEST_F(WebRtcSessionTest, TestAVOfferWithAudioOnlyAnswer) {
  Init();
  EXPECT_TRUE(media_engine_->GetVideoChannel(0) == NULL);
  EXPECT_TRUE(media_engine_->GetVoiceChannel(0) == NULL);

  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();

  cricket::MediaSessionOptions options;
  SessionDescriptionInterface* answer = CreateRemoteAnswer(offer, options);

  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer;
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(video_channel_ == NULL);

  ASSERT_EQ(0u, voice_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack1, voice_channel_->send_streams()[0].id);

  // Let the remote end update the session descriptions, with Audio and Video.
  SendAudioVideoStream2();
  CreateAndSetRemoteOfferAndLocalAnswer();

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(video_channel_ != NULL);
  ASSERT_TRUE(voice_channel_ != NULL);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].id);
  EXPECT_EQ(kVideoTrack2, video_channel_->send_streams()[0].id);
  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].id);
  EXPECT_EQ(kAudioTrack2, voice_channel_->send_streams()[0].id);

  // Change session back to audio only.
  SendAudioOnlyStream2();
  CreateAndSetRemoteOfferAndLocalAnswer();

  EXPECT_EQ(0u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].id);
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->send_streams()[0].id);
}

// This test verifies the call setup when remote answer with video only and
// later updates with audio.
TEST_F(WebRtcSessionTest, TestAVOfferWithVideoOnlyAnswer) {
  Init();
  EXPECT_TRUE(media_engine_->GetVideoChannel(0) == NULL);
  EXPECT_TRUE(media_engine_->GetVoiceChannel(0) == NULL);
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();

  cricket::MediaSessionOptions options;
  options.recv_audio = false;
  options.recv_video = true;
  SessionDescriptionInterface* answer = CreateRemoteAnswer(
      offer, options, cricket::SEC_ENABLED);

  // SetLocalDescription and SetRemoteDescriptions takes ownership of offer
  // and answer.
  SetLocalDescriptionWithoutError(offer);
  SetRemoteDescriptionWithoutError(answer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(voice_channel_ == NULL);
  ASSERT_TRUE(video_channel_ != NULL);

  EXPECT_EQ(0u, video_channel_->recv_streams().size());
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack1, video_channel_->send_streams()[0].id);

  // Update the session descriptions, with Audio and Video.
  SendAudioVideoStream2();
  CreateAndSetRemoteOfferAndLocalAnswer();

  voice_channel_ = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(voice_channel_ != NULL);

  ASSERT_EQ(1u, voice_channel_->recv_streams().size());
  ASSERT_EQ(1u, voice_channel_->send_streams().size());
  EXPECT_EQ(kAudioTrack2, voice_channel_->recv_streams()[0].id);
  EXPECT_EQ(kAudioTrack2, voice_channel_->send_streams()[0].id);

  // Change session back to video only.
  SendVideoOnlyStream2();
  CreateAndSetRemoteOfferAndLocalAnswer();

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_EQ(1u, video_channel_->recv_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->recv_streams()[0].id);
  ASSERT_EQ(1u, video_channel_->send_streams().size());
  EXPECT_EQ(kVideoTrack2, video_channel_->send_streams()[0].id);
}

TEST_F(WebRtcSessionTest, VerifyCryptoParamsInSDP) {
  Init();
  SendAudioVideoStream1();
  scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());
  VerifyCryptoParams(offer->description());
  SetRemoteDescriptionWithoutError(offer.release());
  scoped_ptr<SessionDescriptionInterface> answer(CreateAnswer(NULL));
  VerifyCryptoParams(answer->description());
}

TEST_F(WebRtcSessionTest, VerifyNoCryptoParamsInSDP) {
  options_.disable_encryption = true;
  Init();
  SendAudioVideoStream1();
  scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());
  VerifyNoCryptoParams(offer->description(), false);
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromNonCryptoOffer) {
  Init();
  VerifyAnswerFromNonCryptoOffer();
}

TEST_F(WebRtcSessionTest, VerifyAnswerFromCryptoOffer) {
  Init();
  VerifyAnswerFromCryptoOffer();
}

// This test verifies that setLocalDescription fails if
// no a=ice-ufrag and a=ice-pwd lines are present in the SDP.
TEST_F(WebRtcSessionTest, TestSetLocalDescriptionWithoutIce) {
  Init();
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  std::string sdp;
  RemoveIceUfragPwdLines(offer.get(), &sdp);
  SessionDescriptionInterface* modified_offer =
    CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  SetLocalDescriptionOfferExpectError(kSdpWithoutIceUfragPwd, modified_offer);
}

// This test verifies that setRemoteDescription fails if
// no a=ice-ufrag and a=ice-pwd lines are present in the SDP.
TEST_F(WebRtcSessionTest, TestSetRemoteDescriptionWithoutIce) {
  Init();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateRemoteOffer());
  std::string sdp;
  RemoveIceUfragPwdLines(offer.get(), &sdp);
  SessionDescriptionInterface* modified_offer =
    CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  SetRemoteDescriptionOfferExpectError(kSdpWithoutIceUfragPwd, modified_offer);
}

// This test verifies that setLocalDescription fails if local offer has
// too short ice ufrag and pwd strings.
TEST_F(WebRtcSessionTest, TestSetLocalDescriptionInvalidIceCredentials) {
  Init();
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  std::string sdp;
  // Modifying ice ufrag and pwd in local offer with strings smaller than the
  // recommended values of 4 and 22 bytes respectively.
  ModifyIceUfragPwdLines(offer.get(), "ice", "icepwd", &sdp);
  SessionDescriptionInterface* modified_offer =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  std::string error;
  EXPECT_FALSE(session_->SetLocalDescription(modified_offer, &error));

  // Test with string greater than 256.
  sdp.clear();
  ModifyIceUfragPwdLines(offer.get(), kTooLongIceUfragPwd, kTooLongIceUfragPwd,
                         &sdp);
  modified_offer = CreateSessionDescription(JsepSessionDescription::kOffer, sdp,
                                            NULL);
  EXPECT_FALSE(session_->SetLocalDescription(modified_offer, &error));
}

// This test verifies that setRemoteDescription fails if remote offer has
// too short ice ufrag and pwd strings.
TEST_F(WebRtcSessionTest, TestSetRemoteDescriptionInvalidIceCredentials) {
  Init();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateRemoteOffer());
  std::string sdp;
  // Modifying ice ufrag and pwd in remote offer with strings smaller than the
  // recommended values of 4 and 22 bytes respectively.
  ModifyIceUfragPwdLines(offer.get(), "ice", "icepwd", &sdp);
  SessionDescriptionInterface* modified_offer =
     CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  std::string error;
  EXPECT_FALSE(session_->SetRemoteDescription(modified_offer, &error));

  sdp.clear();
  ModifyIceUfragPwdLines(offer.get(), kTooLongIceUfragPwd, kTooLongIceUfragPwd,
                         &sdp);
  modified_offer = CreateSessionDescription(JsepSessionDescription::kOffer, sdp,
                                            NULL);
  EXPECT_FALSE(session_->SetRemoteDescription(modified_offer, &error));
}

// Test that if the remote description indicates the peer requested ICE restart
// (via a new ufrag or pwd), the old ICE candidates are not copied,
// and vice versa.
TEST_F(WebRtcSessionTest, TestSetRemoteDescriptionWithIceRestart) {
  Init();
  scoped_ptr<SessionDescriptionInterface> offer(CreateRemoteOffer());

  // Create the first offer.
  std::string sdp;
  ModifyIceUfragPwdLines(offer.get(), "0123456789012345",
                         "abcdefghijklmnopqrstuvwx", &sdp);
  SessionDescriptionInterface* offer1 =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  cricket::Candidate candidate1(1, "udp", rtc::SocketAddress("1.1.1.1", 5000),
                                0, "", "", "relay", 0, "");
  JsepIceCandidate ice_candidate1(kMediaContentName0, kMediaContentIndex0,
                                  candidate1);
  EXPECT_TRUE(offer1->AddCandidate(&ice_candidate1));
  SetRemoteDescriptionWithoutError(offer1);
  EXPECT_EQ(1, session_->remote_description()->candidates(0)->count());

  // The second offer has the same ufrag and pwd but different address.
  sdp.clear();
  ModifyIceUfragPwdLines(offer.get(), "0123456789012345",
                         "abcdefghijklmnopqrstuvwx", &sdp);
  SessionDescriptionInterface* offer2 =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  candidate1.set_address(rtc::SocketAddress("1.1.1.1", 6000));
  JsepIceCandidate ice_candidate2(kMediaContentName0, kMediaContentIndex0,
                                  candidate1);
  EXPECT_TRUE(offer2->AddCandidate(&ice_candidate2));
  SetRemoteDescriptionWithoutError(offer2);
  EXPECT_EQ(2, session_->remote_description()->candidates(0)->count());

  // The third offer has a different ufrag and different address.
  sdp.clear();
  ModifyIceUfragPwdLines(offer.get(), "0123456789012333",
                         "abcdefghijklmnopqrstuvwx", &sdp);
  SessionDescriptionInterface* offer3 =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  candidate1.set_address(rtc::SocketAddress("1.1.1.1", 7000));
  JsepIceCandidate ice_candidate3(kMediaContentName0, kMediaContentIndex0,
                                  candidate1);
  EXPECT_TRUE(offer3->AddCandidate(&ice_candidate3));
  SetRemoteDescriptionWithoutError(offer3);
  EXPECT_EQ(1, session_->remote_description()->candidates(0)->count());

  // The fourth offer has no candidate but a different ufrag/pwd.
  sdp.clear();
  ModifyIceUfragPwdLines(offer.get(), "0123456789012444",
                         "abcdefghijklmnopqrstuvyz", &sdp);
  SessionDescriptionInterface* offer4 =
      CreateSessionDescription(JsepSessionDescription::kOffer, sdp, NULL);
  SetRemoteDescriptionWithoutError(offer4);
  EXPECT_EQ(0, session_->remote_description()->candidates(0)->count());
}

// Test that candidates sent to the "video" transport do not get pushed down to
// the "audio" transport channel when bundling.
TEST_F(WebRtcSessionTest, TestIgnoreCandidatesForUnusedTransportWhenBundling) {
  AddInterface(rtc::SocketAddress(kClientAddrHost1, kClientAddrPort));

  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyBalanced);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateRemoteOffer();
  SetRemoteDescriptionWithoutError(offer);

  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  SetLocalDescriptionWithoutError(answer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  cricket::BaseChannel* voice_channel = session_->voice_channel();
  ASSERT(voice_channel != NULL);

  // Checks if one of the transport channels contains a connection using a given
  // port.
  auto connection_with_remote_port = [this, voice_channel](int port) {
    SessionStats stats;
    session_->GetChannelTransportStats(voice_channel, &stats);
    for (auto& kv : stats.transport_stats) {
      for (auto& chan_stat : kv.second.channel_stats) {
        for (auto& conn_info : chan_stat.connection_infos) {
          if (conn_info.remote_candidate.address().port() == port) {
            return true;
          }
        }
      }
    }
    return false;
  };

  EXPECT_FALSE(connection_with_remote_port(5000));
  EXPECT_FALSE(connection_with_remote_port(5001));
  EXPECT_FALSE(connection_with_remote_port(6000));

  // The way the *_WAIT checks work is they only wait if the condition fails,
  // which does not help in the case where state is not changing. This is
  // problematic in this test since we want to verify that adding a video
  // candidate does _not_ change state. So we interleave candidates and assume
  // that messages are executed in the order they were posted.

  // First audio candidate.
  cricket::Candidate candidate0;
  candidate0.set_address(rtc::SocketAddress("1.1.1.1", 5000));
  candidate0.set_component(1);
  candidate0.set_protocol("udp");
  JsepIceCandidate ice_candidate0(kMediaContentName0, kMediaContentIndex0,
                                  candidate0);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate0));

  // Video candidate.
  cricket::Candidate candidate1;
  candidate1.set_address(rtc::SocketAddress("1.1.1.1", 6000));
  candidate1.set_component(1);
  candidate1.set_protocol("udp");
  JsepIceCandidate ice_candidate1(kMediaContentName1, kMediaContentIndex1,
                                  candidate1);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate1));

  // Second audio candidate.
  cricket::Candidate candidate2;
  candidate2.set_address(rtc::SocketAddress("1.1.1.1", 5001));
  candidate2.set_component(1);
  candidate2.set_protocol("udp");
  JsepIceCandidate ice_candidate2(kMediaContentName0, kMediaContentIndex0,
                                  candidate2);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate2));

  EXPECT_TRUE_WAIT(connection_with_remote_port(5000), 1000);
  EXPECT_TRUE_WAIT(connection_with_remote_port(5001), 1000);

  // No need here for a _WAIT check since we are checking that state hasn't
  // changed: if this is false we would be doing waits for nothing and if this
  // is true then there will be no messages processed anyways.
  EXPECT_FALSE(connection_with_remote_port(6000));
}

// kBundlePolicyBalanced BUNDLE policy and answer contains BUNDLE.
TEST_F(WebRtcSessionTest, TestBalancedBundleInAnswer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyBalanced);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyBalanced BUNDLE policy but no BUNDLE in the answer.
TEST_F(WebRtcSessionTest, TestBalancedNoBundleInAnswer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyBalanced);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();

  // Remove BUNDLE from the answer.
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateRemoteAnswer(session_->local_description()));
  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);
  modified_answer->Initialize(answer_copy, "1", "1");
  SetRemoteDescriptionWithoutError(modified_answer);  //

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyMaxBundle policy with BUNDLE in the answer.
TEST_F(WebRtcSessionTest, TestMaxBundleBundleInAnswer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxBundle);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyMaxBundle policy with BUNDLE in the answer, but no
// audio content in the answer.
TEST_F(WebRtcSessionTest, TestMaxBundleRejectAudio) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxBundle);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();
  cricket::MediaSessionOptions recv_options;
  recv_options.recv_audio = false;
  recv_options.recv_video = true;
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description(), recv_options);
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_TRUE(nullptr == session_->voice_channel());
  EXPECT_TRUE(nullptr != session_->video_rtp_transport_channel());

  session_->Close();
  EXPECT_TRUE(nullptr == session_->voice_rtp_transport_channel());
  EXPECT_TRUE(nullptr == session_->voice_rtcp_transport_channel());
  EXPECT_TRUE(nullptr == session_->video_rtp_transport_channel());
  EXPECT_TRUE(nullptr == session_->video_rtcp_transport_channel());
}

// kBundlePolicyMaxBundle policy but no BUNDLE in the answer.
TEST_F(WebRtcSessionTest, TestMaxBundleNoBundleInAnswer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxBundle);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();

  // Remove BUNDLE from the answer.
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateRemoteAnswer(session_->local_description()));
  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);
  modified_answer->Initialize(answer_copy, "1", "1");
  SetRemoteDescriptionWithoutError(modified_answer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyMaxBundle policy with BUNDLE in the remote offer.
TEST_F(WebRtcSessionTest, TestMaxBundleBundleInRemoteOffer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxBundle);
  SendAudioVideoStream1();

  SessionDescriptionInterface* offer = CreateRemoteOffer();
  SetRemoteDescriptionWithoutError(offer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer = CreateAnswer(nullptr);
  SetLocalDescriptionWithoutError(answer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyMaxBundle policy but no BUNDLE in the remote offer.
TEST_F(WebRtcSessionTest, TestMaxBundleNoBundleInRemoteOffer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxBundle);
  SendAudioVideoStream1();

  // Remove BUNDLE from the offer.
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateRemoteOffer());
  cricket::SessionDescription* offer_copy = offer->description()->Copy();
  offer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_offer =
      new JsepSessionDescription(JsepSessionDescription::kOffer);
  modified_offer->Initialize(offer_copy, "1", "1");

  // Expect an error when applying the remote description
  SetRemoteDescriptionExpectError(JsepSessionDescription::kOffer,
                                  kCreateChannelFailed, modified_offer);
}

// kBundlePolicyMaxCompat bundle policy and answer contains BUNDLE.
TEST_F(WebRtcSessionTest, TestMaxCompatBundleInAnswer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxCompat);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  // This should lead to an audio-only call but isn't implemented
  // correctly yet.
  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyMaxCompat BUNDLE policy but no BUNDLE in the answer.
TEST_F(WebRtcSessionTest, TestMaxCompatNoBundleInAnswer) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxCompat);
  SendAudioVideoStream1();
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();

  // Remove BUNDLE from the answer.
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateRemoteAnswer(session_->local_description()));
  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveGroupByName(cricket::GROUP_TYPE_BUNDLE);
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);
  modified_answer->Initialize(answer_copy, "1", "1");
  SetRemoteDescriptionWithoutError(modified_answer);  //

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

// kBundlePolicyMaxbundle and then we call SetRemoteDescription first.
TEST_F(WebRtcSessionTest, TestMaxBundleWithSetRemoteDescriptionFirst) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyMaxBundle);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetRemoteDescriptionWithoutError(offer);

  EXPECT_EQ(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());
}

TEST_F(WebRtcSessionTest, TestRequireRtcpMux) {
  InitWithRtcpMuxPolicy(PeerConnectionInterface::kRtcpMuxPolicyRequire);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_TRUE(session_->voice_rtcp_transport_channel() == NULL);
  EXPECT_TRUE(session_->video_rtcp_transport_channel() == NULL);

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_TRUE(session_->voice_rtcp_transport_channel() == NULL);
  EXPECT_TRUE(session_->video_rtcp_transport_channel() == NULL);
}

TEST_F(WebRtcSessionTest, TestNegotiateRtcpMux) {
  InitWithRtcpMuxPolicy(PeerConnectionInterface::kRtcpMuxPolicyNegotiate);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  EXPECT_TRUE(session_->voice_rtcp_transport_channel() != NULL);
  EXPECT_TRUE(session_->video_rtcp_transport_channel() != NULL);

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_TRUE(session_->voice_rtcp_transport_channel() == NULL);
  EXPECT_TRUE(session_->video_rtcp_transport_channel() == NULL);
}

// This test verifies that SetLocalDescription and SetRemoteDescription fails
// if BUNDLE is enabled but rtcp-mux is disabled in m-lines.
TEST_F(WebRtcSessionTest, TestDisabledRtcpMuxWithBundleEnabled) {
  Init();
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  std::string offer_str;
  offer->ToString(&offer_str);
  // Disable rtcp-mux
  const std::string rtcp_mux = "rtcp-mux";
  const std::string xrtcp_mux = "xrtcp-mux";
  rtc::replace_substrs(rtcp_mux.c_str(), rtcp_mux.length(),
                             xrtcp_mux.c_str(), xrtcp_mux.length(),
                             &offer_str);
  JsepSessionDescription* local_offer =
      new JsepSessionDescription(JsepSessionDescription::kOffer);
  EXPECT_TRUE((local_offer)->Initialize(offer_str, NULL));
  SetLocalDescriptionOfferExpectError(kBundleWithoutRtcpMux, local_offer);
  JsepSessionDescription* remote_offer =
      new JsepSessionDescription(JsepSessionDescription::kOffer);
  EXPECT_TRUE((remote_offer)->Initialize(offer_str, NULL));
  SetRemoteDescriptionOfferExpectError(kBundleWithoutRtcpMux, remote_offer);
  // Trying unmodified SDP.
  SetLocalDescriptionWithoutError(offer);
}

TEST_F(WebRtcSessionTest, SetAudioPlayout) {
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();
  cricket::FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->recv_streams().size());
  uint32_t receive_ssrc = channel->recv_streams()[0].first_ssrc();
  double volume;
  EXPECT_TRUE(channel->GetOutputVolume(receive_ssrc, &volume));
  EXPECT_EQ(1, volume);
  session_->SetAudioPlayout(receive_ssrc, false);
  EXPECT_TRUE(channel->GetOutputVolume(receive_ssrc, &volume));
  EXPECT_EQ(0, volume);
  session_->SetAudioPlayout(receive_ssrc, true);
  EXPECT_TRUE(channel->GetOutputVolume(receive_ssrc, &volume));
  EXPECT_EQ(1, volume);
}

TEST_F(WebRtcSessionTest, SetAudioSend) {
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();
  cricket::FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32_t send_ssrc = channel->send_streams()[0].first_ssrc();
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));

  cricket::AudioOptions options;
  options.echo_cancellation = rtc::Optional<bool>(true);

  rtc::scoped_ptr<FakeAudioRenderer> renderer(new FakeAudioRenderer());
  session_->SetAudioSend(send_ssrc, false, options, renderer.get());
  EXPECT_TRUE(channel->IsStreamMuted(send_ssrc));
  EXPECT_EQ(rtc::Optional<bool>(), channel->options().echo_cancellation);
  EXPECT_TRUE(renderer->sink() != NULL);

  // This will trigger SetSink(NULL) to the |renderer|.
  session_->SetAudioSend(send_ssrc, true, options, NULL);
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
  EXPECT_EQ(rtc::Optional<bool>(true), channel->options().echo_cancellation);
  EXPECT_TRUE(renderer->sink() == NULL);
}

TEST_F(WebRtcSessionTest, AudioRendererForLocalStream) {
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();
  cricket::FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32_t send_ssrc = channel->send_streams()[0].first_ssrc();

  rtc::scoped_ptr<FakeAudioRenderer> renderer(new FakeAudioRenderer());
  cricket::AudioOptions options;
  session_->SetAudioSend(send_ssrc, true, options, renderer.get());
  EXPECT_TRUE(renderer->sink() != NULL);

  // Delete the |renderer| and it will trigger OnClose() to the sink, and this
  // will invalidate the |renderer_| pointer in the sink and prevent getting a
  // SetSink(NULL) callback afterwards.
  renderer.reset();

  // This will trigger SetSink(NULL) if no OnClose() callback.
  session_->SetAudioSend(send_ssrc, true, options, NULL);
}

TEST_F(WebRtcSessionTest, SetVideoPlayout) {
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();
  cricket::FakeVideoMediaChannel* channel = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_LT(0u, channel->renderers().size());
  EXPECT_TRUE(channel->renderers().begin()->second == NULL);
  ASSERT_EQ(1u, channel->recv_streams().size());
  uint32_t receive_ssrc = channel->recv_streams()[0].first_ssrc();
  cricket::FakeVideoRenderer renderer;
  session_->SetVideoPlayout(receive_ssrc, true, &renderer);
  EXPECT_TRUE(channel->renderers().begin()->second == &renderer);
  session_->SetVideoPlayout(receive_ssrc, false, &renderer);
  EXPECT_TRUE(channel->renderers().begin()->second == NULL);
}

TEST_F(WebRtcSessionTest, SetVideoSend) {
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();
  cricket::FakeVideoMediaChannel* channel = media_engine_->GetVideoChannel(0);
  ASSERT_TRUE(channel != NULL);
  ASSERT_EQ(1u, channel->send_streams().size());
  uint32_t send_ssrc = channel->send_streams()[0].first_ssrc();
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
  cricket::VideoOptions* options = NULL;
  session_->SetVideoSend(send_ssrc, false, options);
  EXPECT_TRUE(channel->IsStreamMuted(send_ssrc));
  session_->SetVideoSend(send_ssrc, true, options);
  EXPECT_FALSE(channel->IsStreamMuted(send_ssrc));
}

TEST_F(WebRtcSessionTest, CanNotInsertDtmf) {
  TestCanInsertDtmf(false);
}

TEST_F(WebRtcSessionTest, CanInsertDtmf) {
  TestCanInsertDtmf(true);
}

TEST_F(WebRtcSessionTest, InsertDtmf) {
  // Setup
  Init();
  SendAudioVideoStream1();
  CreateAndSetRemoteOfferAndLocalAnswer();
  FakeVoiceMediaChannel* channel = media_engine_->GetVoiceChannel(0);
  EXPECT_EQ(0U, channel->dtmf_info_queue().size());

  // Insert DTMF
  const int expected_flags = DF_SEND;
  const int expected_duration = 90;
  session_->InsertDtmf(kAudioTrack1, 0, expected_duration);
  session_->InsertDtmf(kAudioTrack1, 1, expected_duration);
  session_->InsertDtmf(kAudioTrack1, 2, expected_duration);

  // Verify
  ASSERT_EQ(3U, channel->dtmf_info_queue().size());
  const uint32_t send_ssrc = channel->send_streams()[0].first_ssrc();
  EXPECT_TRUE(CompareDtmfInfo(channel->dtmf_info_queue()[0], send_ssrc, 0,
                              expected_duration, expected_flags));
  EXPECT_TRUE(CompareDtmfInfo(channel->dtmf_info_queue()[1], send_ssrc, 1,
                              expected_duration, expected_flags));
  EXPECT_TRUE(CompareDtmfInfo(channel->dtmf_info_queue()[2], send_ssrc, 2,
                              expected_duration, expected_flags));
}

// This test verifies the |initial_offerer| flag when session initiates the
// call.
TEST_F(WebRtcSessionTest, TestInitiatorFlagAsOriginator) {
  Init();
  EXPECT_FALSE(session_->initial_offerer());
  SessionDescriptionInterface* offer = CreateOffer();
  SessionDescriptionInterface* answer = CreateRemoteAnswer(offer);
  SetLocalDescriptionWithoutError(offer);
  EXPECT_TRUE(session_->initial_offerer());
  SetRemoteDescriptionWithoutError(answer);
  EXPECT_TRUE(session_->initial_offerer());
}

// This test verifies the |initial_offerer| flag when session receives the call.
TEST_F(WebRtcSessionTest, TestInitiatorFlagAsReceiver) {
  Init();
  EXPECT_FALSE(session_->initial_offerer());
  SessionDescriptionInterface* offer = CreateRemoteOffer();
  SetRemoteDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer = CreateAnswer(NULL);

  EXPECT_FALSE(session_->initial_offerer());
  SetLocalDescriptionWithoutError(answer);
  EXPECT_FALSE(session_->initial_offerer());
}

// Verifing local offer and remote answer have matching m-lines as per RFC 3264.
TEST_F(WebRtcSessionTest, TestIncorrectMLinesInRemoteAnswer) {
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateRemoteAnswer(session_->local_description()));

  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveContentByName("video");
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);

  EXPECT_TRUE(modified_answer->Initialize(answer_copy,
                                          answer->session_id(),
                                          answer->session_version()));
  SetRemoteDescriptionAnswerExpectError(kMlineMismatch, modified_answer);

  // Different content names.
  std::string sdp;
  EXPECT_TRUE(answer->ToString(&sdp));
  const std::string kAudioMid = "a=mid:audio";
  const std::string kAudioMidReplaceStr = "a=mid:audio_content_name";
  rtc::replace_substrs(kAudioMid.c_str(), kAudioMid.length(),
                             kAudioMidReplaceStr.c_str(),
                             kAudioMidReplaceStr.length(),
                             &sdp);
  SessionDescriptionInterface* modified_answer1 =
      CreateSessionDescription(JsepSessionDescription::kAnswer, sdp, NULL);
  SetRemoteDescriptionAnswerExpectError(kMlineMismatch, modified_answer1);

  // Different media types.
  EXPECT_TRUE(answer->ToString(&sdp));
  const std::string kAudioMline = "m=audio";
  const std::string kAudioMlineReplaceStr = "m=video";
  rtc::replace_substrs(kAudioMline.c_str(), kAudioMline.length(),
                             kAudioMlineReplaceStr.c_str(),
                             kAudioMlineReplaceStr.length(),
                             &sdp);
  SessionDescriptionInterface* modified_answer2 =
      CreateSessionDescription(JsepSessionDescription::kAnswer, sdp, NULL);
  SetRemoteDescriptionAnswerExpectError(kMlineMismatch, modified_answer2);

  SetRemoteDescriptionWithoutError(answer.release());
}

// Verifying remote offer and local answer have matching m-lines as per
// RFC 3264.
TEST_F(WebRtcSessionTest, TestIncorrectMLinesInLocalAnswer) {
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateRemoteOffer();
  SetRemoteDescriptionWithoutError(offer);
  SessionDescriptionInterface* answer = CreateAnswer(NULL);

  cricket::SessionDescription* answer_copy = answer->description()->Copy();
  answer_copy->RemoveContentByName("video");
  JsepSessionDescription* modified_answer =
      new JsepSessionDescription(JsepSessionDescription::kAnswer);

  EXPECT_TRUE(modified_answer->Initialize(answer_copy,
                                          answer->session_id(),
                                          answer->session_version()));
  SetLocalDescriptionAnswerExpectError(kMlineMismatch, modified_answer);
  SetLocalDescriptionWithoutError(answer);
}

// This test verifies that WebRtcSession does not start candidate allocation
// before SetLocalDescription is called.
TEST_F(WebRtcSessionTest, TestIceStartAfterSetLocalDescriptionOnly) {
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateRemoteOffer();
  cricket::Candidate candidate;
  candidate.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName0, kMediaContentIndex0,
                                 candidate);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate));
  cricket::Candidate candidate1;
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate1(kMediaContentName1, kMediaContentIndex1,
                                  candidate1);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate1));
  SetRemoteDescriptionWithoutError(offer);
  ASSERT_TRUE(session_->voice_rtp_transport_channel() != NULL);
  ASSERT_TRUE(session_->video_rtp_transport_channel() != NULL);

  // Pump for 1 second and verify that no candidates are generated.
  rtc::Thread::Current()->ProcessMessages(1000);
  EXPECT_TRUE(observer_.mline_0_candidates_.empty());
  EXPECT_TRUE(observer_.mline_1_candidates_.empty());

  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  SetLocalDescriptionWithoutError(answer);
  EXPECT_TRUE_WAIT(observer_.oncandidatesready_, kIceCandidatesTimeout);
}

// This test verifies that crypto parameter is updated in local session
// description as per security policy set in MediaSessionDescriptionFactory.
TEST_F(WebRtcSessionTest, TestCryptoAfterSetLocalDescription) {
  Init();
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  // Making sure SetLocalDescription correctly sets crypto value in
  // SessionDescription object after de-serialization of sdp string. The value
  // will be set as per MediaSessionDescriptionFactory.
  std::string offer_str;
  offer->ToString(&offer_str);
  SessionDescriptionInterface* jsep_offer_str =
      CreateSessionDescription(JsepSessionDescription::kOffer, offer_str, NULL);
  SetLocalDescriptionWithoutError(jsep_offer_str);
  EXPECT_TRUE(session_->voice_channel()->secure_required());
  EXPECT_TRUE(session_->video_channel()->secure_required());
}

// This test verifies the crypto parameter when security is disabled.
TEST_F(WebRtcSessionTest, TestCryptoAfterSetLocalDescriptionWithDisabled) {
  options_.disable_encryption = true;
  Init();
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  // Making sure SetLocalDescription correctly sets crypto value in
  // SessionDescription object after de-serialization of sdp string. The value
  // will be set as per MediaSessionDescriptionFactory.
  std::string offer_str;
  offer->ToString(&offer_str);
  SessionDescriptionInterface* jsep_offer_str =
      CreateSessionDescription(JsepSessionDescription::kOffer, offer_str, NULL);
  SetLocalDescriptionWithoutError(jsep_offer_str);
  EXPECT_FALSE(session_->voice_channel()->secure_required());
  EXPECT_FALSE(session_->video_channel()->secure_required());
}

// This test verifies that an answer contains new ufrag and password if an offer
// with new ufrag and password is received.
TEST_F(WebRtcSessionTest, TestCreateAnswerWithNewUfragAndPassword) {
  Init();
  cricket::MediaSessionOptions options;
  options.recv_video = true;
  rtc::scoped_ptr<JsepSessionDescription> offer(
      CreateRemoteOffer(options));
  SetRemoteDescriptionWithoutError(offer.release());

  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(NULL));
  SetLocalDescriptionWithoutError(answer.release());

  // Receive an offer with new ufrag and password.
  options.transport_options.ice_restart = true;
  rtc::scoped_ptr<JsepSessionDescription> updated_offer1(
      CreateRemoteOffer(options, session_->remote_description()));
  SetRemoteDescriptionWithoutError(updated_offer1.release());

  rtc::scoped_ptr<SessionDescriptionInterface> updated_answer1(
      CreateAnswer(NULL));

  CompareIceUfragAndPassword(updated_answer1->description(),
                             session_->local_description()->description(),
                             false);

  SetLocalDescriptionWithoutError(updated_answer1.release());
}

// This test verifies that an answer contains old ufrag and password if an offer
// with old ufrag and password is received.
TEST_F(WebRtcSessionTest, TestCreateAnswerWithOldUfragAndPassword) {
  Init();
  cricket::MediaSessionOptions options;
  options.recv_video = true;
  rtc::scoped_ptr<JsepSessionDescription> offer(
      CreateRemoteOffer(options));
  SetRemoteDescriptionWithoutError(offer.release());

  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> answer(
      CreateAnswer(NULL));
  SetLocalDescriptionWithoutError(answer.release());

  // Receive an offer without changed ufrag or password.
  options.transport_options.ice_restart = false;
  rtc::scoped_ptr<JsepSessionDescription> updated_offer2(
      CreateRemoteOffer(options, session_->remote_description()));
  SetRemoteDescriptionWithoutError(updated_offer2.release());

  rtc::scoped_ptr<SessionDescriptionInterface> updated_answer2(
      CreateAnswer(NULL));

  CompareIceUfragAndPassword(updated_answer2->description(),
                             session_->local_description()->description(),
                             true);

  SetLocalDescriptionWithoutError(updated_answer2.release());
}

TEST_F(WebRtcSessionTest, TestSessionContentError) {
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();
  const std::string session_id_orig = offer->session_id();
  const std::string session_version_orig = offer->session_version();
  SetLocalDescriptionWithoutError(offer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  video_channel_->set_fail_set_send_codecs(true);

  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionAnswerExpectError("ERROR_CONTENT", answer);

  // Test that after a content error, setting any description will
  // result in an error.
  video_channel_->set_fail_set_send_codecs(false);
  answer = CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionExpectError("", "ERROR_CONTENT", answer);
  offer = CreateRemoteOffer();
  SetLocalDescriptionExpectError("", "ERROR_CONTENT", offer);
}

// Runs the loopback call test with BUNDLE and STUN disabled.
TEST_F(WebRtcSessionTest, TestIceStatesBasic) {
  // Lets try with only UDP ports.
  allocator_->set_flags(cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY);
  TestLoopbackCall();
}

TEST_F(WebRtcSessionTest, TestIceStatesBasicIPv6) {
  allocator_->set_flags(cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_ENABLE_IPV6 |
                        cricket::PORTALLOCATOR_DISABLE_RELAY);

  // best connection is IPv6 since it has higher network preference.
  LoopbackNetworkConfiguration config;
  config.test_ipv6_network_ = true;
  config.best_connection_after_initial_ice_converged_ =
      LoopbackNetworkConfiguration::ExpectedBestConnection(0, 1);

  TestLoopbackCall(config);
}

// Runs the loopback call test with BUNDLE and STUN enabled.
TEST_F(WebRtcSessionTest, TestIceStatesBundle) {
  allocator_->set_flags(cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_RELAY);
  TestLoopbackCall();
}

TEST_F(WebRtcSessionTest, TestRtpDataChannel) {
  constraints_.reset(new FakeConstraints());
  constraints_->AddOptional(
      webrtc::MediaConstraintsInterface::kEnableRtpDataChannels, true);
  Init();

  SetLocalDescriptionWithDataChannel();
  EXPECT_EQ(cricket::DCT_RTP, data_engine_->last_channel_type());
}

TEST_P(WebRtcSessionTest, TestRtpDataChannelConstraintTakesPrecedence) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);

  constraints_.reset(new FakeConstraints());
  constraints_->AddOptional(
      webrtc::MediaConstraintsInterface::kEnableRtpDataChannels, true);
  options_.disable_sctp_data_channels = false;

  InitWithDtls(GetParam());

  SetLocalDescriptionWithDataChannel();
  EXPECT_EQ(cricket::DCT_RTP, data_engine_->last_channel_type());
}

TEST_P(WebRtcSessionTest, TestCreateOfferWithSctpEnabledWithoutStreams) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);

  InitWithDtls(GetParam());

  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());
  EXPECT_TRUE(offer->description()->GetContentByName("data") == NULL);
  EXPECT_TRUE(offer->description()->GetTransportInfoByName("data") == NULL);
}

TEST_P(WebRtcSessionTest, TestCreateAnswerWithSctpInOfferAndNoStreams) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  SetFactoryDtlsSrtp();
  InitWithDtls(GetParam());

  // Create remote offer with SCTP.
  cricket::MediaSessionOptions options;
  options.data_channel_type = cricket::DCT_SCTP;
  JsepSessionDescription* offer =
      CreateRemoteOffer(options, cricket::SEC_DISABLED);
  SetRemoteDescriptionWithoutError(offer);

  // Verifies the answer contains SCTP.
  rtc::scoped_ptr<SessionDescriptionInterface> answer(CreateAnswer(NULL));
  EXPECT_TRUE(answer != NULL);
  EXPECT_TRUE(answer->description()->GetContentByName("data") != NULL);
  EXPECT_TRUE(answer->description()->GetTransportInfoByName("data") != NULL);
}

TEST_P(WebRtcSessionTest, TestSctpDataChannelWithoutDtls) {
  constraints_.reset(new FakeConstraints());
  constraints_->AddOptional(
      webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, false);
  InitWithDtls(GetParam());

  SetLocalDescriptionWithDataChannel();
  EXPECT_EQ(cricket::DCT_NONE, data_engine_->last_channel_type());
}

TEST_P(WebRtcSessionTest, TestSctpDataChannelWithDtls) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);

  InitWithDtls(GetParam());

  SetLocalDescriptionWithDataChannel();
  EXPECT_EQ(cricket::DCT_SCTP, data_engine_->last_channel_type());
}

TEST_P(WebRtcSessionTest, TestDisableSctpDataChannels) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  options_.disable_sctp_data_channels = true;
  InitWithDtls(GetParam());

  SetLocalDescriptionWithDataChannel();
  EXPECT_EQ(cricket::DCT_NONE, data_engine_->last_channel_type());
}

TEST_P(WebRtcSessionTest, TestSctpDataChannelSendPortParsing) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  const int new_send_port = 9998;
  const int new_recv_port = 7775;

  InitWithDtls(GetParam());
  SetFactoryDtlsSrtp();

  // By default, don't actually add the codecs to desc_factory_; they don't
  // actually get serialized for SCTP in BuildMediaDescription().  Instead,
  // let the session description get parsed.  That'll get the proper codecs
  // into the stream.
  cricket::MediaSessionOptions options;
  JsepSessionDescription* offer = CreateRemoteOfferWithSctpPort(
      "stream1", new_send_port, options);

  // SetRemoteDescription will take the ownership of the offer.
  SetRemoteDescriptionWithoutError(offer);

  SessionDescriptionInterface* answer = ChangeSDPSctpPort(
      new_recv_port, CreateAnswer(NULL));
  ASSERT_TRUE(answer != NULL);

  // Now set the local description, which'll take ownership of the answer.
  SetLocalDescriptionWithoutError(answer);

  // TEST PLAN: Set the port number to something new, set it in the SDP,
  // and pass it all the way down.
  EXPECT_EQ(cricket::DCT_SCTP, data_engine_->last_channel_type());
  CreateDataChannel();

  cricket::FakeDataMediaChannel* ch = data_engine_->GetChannel(0);
  int portnum = -1;
  ASSERT_TRUE(ch != NULL);
  ASSERT_EQ(1UL, ch->send_codecs().size());
  EXPECT_EQ(cricket::kGoogleSctpDataCodecId, ch->send_codecs()[0].id);
  EXPECT_EQ(0, strcmp(cricket::kGoogleSctpDataCodecName,
                      ch->send_codecs()[0].name.c_str()));
  EXPECT_TRUE(ch->send_codecs()[0].GetParam(cricket::kCodecParamPort,
                                            &portnum));
  EXPECT_EQ(new_send_port, portnum);

  ASSERT_EQ(1UL, ch->recv_codecs().size());
  EXPECT_EQ(cricket::kGoogleSctpDataCodecId, ch->recv_codecs()[0].id);
  EXPECT_EQ(0, strcmp(cricket::kGoogleSctpDataCodecName,
                      ch->recv_codecs()[0].name.c_str()));
  EXPECT_TRUE(ch->recv_codecs()[0].GetParam(cricket::kCodecParamPort,
                                            &portnum));
  EXPECT_EQ(new_recv_port, portnum);
}

// Verifies that when a session's DataChannel receives an OPEN message,
// WebRtcSession signals the DataChannel creation request with the expected
// config.
TEST_P(WebRtcSessionTest, TestSctpDataChannelOpenMessage) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);

  InitWithDtls(GetParam());

  SetLocalDescriptionWithDataChannel();
  EXPECT_EQ(cricket::DCT_SCTP, data_engine_->last_channel_type());

  webrtc::DataChannelInit config;
  config.id = 1;
  rtc::Buffer payload;
  webrtc::WriteDataChannelOpenMessage("a", config, &payload);
  cricket::ReceiveDataParams params;
  params.ssrc = config.id;
  params.type = cricket::DMT_CONTROL;

  cricket::DataChannel* data_channel = session_->data_channel();
  data_channel->SignalDataReceived(data_channel, params, payload);

  EXPECT_EQ("a", last_data_channel_label_);
  EXPECT_EQ(config.id, last_data_channel_config_.id);
  EXPECT_FALSE(last_data_channel_config_.negotiated);
  EXPECT_EQ(webrtc::InternalDataChannelInit::kAcker,
            last_data_channel_config_.open_handshake_role);
}

TEST_P(WebRtcSessionTest, TestUsesProvidedCertificate) {
  rtc::scoped_refptr<rtc::RTCCertificate> certificate =
      FakeDtlsIdentityStore::GenerateCertificate();

  PeerConnectionInterface::RTCConfiguration configuration;
  configuration.certificates.push_back(certificate);
  Init(nullptr, configuration);
  EXPECT_TRUE_WAIT(!session_->waiting_for_certificate_for_testing(), 1000);

  EXPECT_EQ(session_->certificate_for_testing(), certificate);
}

// Verifies that CreateOffer succeeds when CreateOffer is called before async
// identity generation is finished (even if a certificate is provided this is
// an async op).
TEST_P(WebRtcSessionTest, TestCreateOfferBeforeIdentityRequestReturnSuccess) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());

  EXPECT_TRUE(session_->waiting_for_certificate_for_testing());
  SendAudioVideoStream1();
  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());

  EXPECT_TRUE(offer != NULL);
  VerifyNoCryptoParams(offer->description(), true);
  VerifyFingerprintStatus(offer->description(), true);
}

// Verifies that CreateAnswer succeeds when CreateOffer is called before async
// identity generation is finished (even if a certificate is provided this is
// an async op).
TEST_P(WebRtcSessionTest, TestCreateAnswerBeforeIdentityRequestReturnSuccess) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());
  SetFactoryDtlsSrtp();

  cricket::MediaSessionOptions options;
  options.recv_video = true;
  scoped_ptr<JsepSessionDescription> offer(
        CreateRemoteOffer(options, cricket::SEC_DISABLED));
  ASSERT_TRUE(offer.get() != NULL);
  SetRemoteDescriptionWithoutError(offer.release());

  rtc::scoped_ptr<SessionDescriptionInterface> answer(CreateAnswer(NULL));
  EXPECT_TRUE(answer != NULL);
  VerifyNoCryptoParams(answer->description(), true);
  VerifyFingerprintStatus(answer->description(), true);
}

// Verifies that CreateOffer succeeds when CreateOffer is called after async
// identity generation is finished (even if a certificate is provided this is
// an async op).
TEST_P(WebRtcSessionTest, TestCreateOfferAfterIdentityRequestReturnSuccess) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());

  EXPECT_TRUE_WAIT(!session_->waiting_for_certificate_for_testing(), 1000);

  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());
  EXPECT_TRUE(offer != NULL);
}

// Verifies that CreateOffer fails when CreateOffer is called after async
// identity generation fails.
TEST_F(WebRtcSessionTest, TestCreateOfferAfterIdentityRequestReturnFailure) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtlsIdentityGenFail();

  EXPECT_TRUE_WAIT(!session_->waiting_for_certificate_for_testing(), 1000);

  rtc::scoped_ptr<SessionDescriptionInterface> offer(CreateOffer());
  EXPECT_TRUE(offer == NULL);
}

// Verifies that CreateOffer succeeds when Multiple CreateOffer calls are made
// before async identity generation is finished.
TEST_P(WebRtcSessionTest,
       TestMultipleCreateOfferBeforeIdentityRequestReturnSuccess) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  VerifyMultipleAsyncCreateDescription(GetParam(),
                                       CreateSessionDescriptionRequest::kOffer);
}

// Verifies that CreateOffer fails when Multiple CreateOffer calls are made
// before async identity generation fails.
TEST_F(WebRtcSessionTest,
       TestMultipleCreateOfferBeforeIdentityRequestReturnFailure) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  VerifyMultipleAsyncCreateDescriptionIdentityGenFailure(
      CreateSessionDescriptionRequest::kOffer);
}

// Verifies that CreateAnswer succeeds when Multiple CreateAnswer calls are made
// before async identity generation is finished.
TEST_P(WebRtcSessionTest,
       TestMultipleCreateAnswerBeforeIdentityRequestReturnSuccess) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  VerifyMultipleAsyncCreateDescription(
      GetParam(), CreateSessionDescriptionRequest::kAnswer);
}

// Verifies that CreateAnswer fails when Multiple CreateAnswer calls are made
// before async identity generation fails.
TEST_F(WebRtcSessionTest,
       TestMultipleCreateAnswerBeforeIdentityRequestReturnFailure) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  VerifyMultipleAsyncCreateDescriptionIdentityGenFailure(
      CreateSessionDescriptionRequest::kAnswer);
}

// Verifies that setRemoteDescription fails when DTLS is disabled and the remote
// offer has no SDES crypto but only DTLS fingerprint.
TEST_F(WebRtcSessionTest, TestSetRemoteOfferFailIfDtlsDisabledAndNoCrypto) {
  // Init without DTLS.
  Init();
  // Create a remote offer with secured transport disabled.
  cricket::MediaSessionOptions options;
  JsepSessionDescription* offer(CreateRemoteOffer(
      options, cricket::SEC_DISABLED));
  // Adds a DTLS fingerprint to the remote offer.
  cricket::SessionDescription* sdp = offer->description();
  TransportInfo* audio = sdp->GetTransportInfoByName("audio");
  ASSERT_TRUE(audio != NULL);
  ASSERT_TRUE(audio->description.identity_fingerprint.get() == NULL);
  audio->description.identity_fingerprint.reset(
      rtc::SSLFingerprint::CreateFromRfc4572(
          rtc::DIGEST_SHA_256, kFakeDtlsFingerprint));
  SetRemoteDescriptionOfferExpectError(kSdpWithoutSdesCrypto,
                                       offer);
}

// This test verifies DSCP is properly applied on the media channels.
TEST_F(WebRtcSessionTest, TestDscpConstraint) {
  constraints_.reset(new FakeConstraints());
  constraints_->AddOptional(
      webrtc::MediaConstraintsInterface::kEnableDscp, true);
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();

  SetLocalDescriptionWithoutError(offer);

  video_channel_ = media_engine_->GetVideoChannel(0);
  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(video_channel_ != NULL);
  ASSERT_TRUE(voice_channel_ != NULL);
  const cricket::AudioOptions& audio_options = voice_channel_->options();
  const cricket::VideoOptions& video_options = video_channel_->options();
  EXPECT_EQ(rtc::Optional<bool>(true), audio_options.dscp);
  EXPECT_EQ(rtc::Optional<bool>(true), video_options.dscp);
}

TEST_F(WebRtcSessionTest, TestSuspendBelowMinBitrateConstraint) {
  constraints_.reset(new FakeConstraints());
  constraints_->AddOptional(
      webrtc::MediaConstraintsInterface::kEnableVideoSuspendBelowMinBitrate,
      true);
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();

  SetLocalDescriptionWithoutError(offer);

  video_channel_ = media_engine_->GetVideoChannel(0);

  ASSERT_TRUE(video_channel_ != NULL);
  const cricket::VideoOptions& video_options = video_channel_->options();
  EXPECT_EQ(rtc::Optional<bool>(true), video_options.suspend_below_min_bitrate);
}

TEST_F(WebRtcSessionTest, TestNumUnsignalledRecvStreamsConstraint) {
  // Number of unsignalled receiving streams should be between 0 and
  // kMaxUnsignalledRecvStreams.
  SetAndVerifyNumUnsignalledRecvStreams(10, 10);
  SetAndVerifyNumUnsignalledRecvStreams(kMaxUnsignalledRecvStreams + 1,
                                        kMaxUnsignalledRecvStreams);
  SetAndVerifyNumUnsignalledRecvStreams(-1, 0);
}

TEST_F(WebRtcSessionTest, TestCombinedAudioVideoBweConstraint) {
  constraints_.reset(new FakeConstraints());
  constraints_->AddOptional(
      webrtc::MediaConstraintsInterface::kCombinedAudioVideoBwe,
      true);
  Init();
  SendAudioVideoStream1();
  SessionDescriptionInterface* offer = CreateOffer();

  SetLocalDescriptionWithoutError(offer);

  voice_channel_ = media_engine_->GetVoiceChannel(0);

  ASSERT_TRUE(voice_channel_ != NULL);
  const cricket::AudioOptions& audio_options = voice_channel_->options();
  EXPECT_EQ(rtc::Optional<bool>(true), audio_options.combined_audio_video_bwe);
}

// Tests that we can renegotiate new media content with ICE candidates in the
// new remote SDP.
TEST_P(WebRtcSessionTest, TestRenegotiateNewMediaWithCandidatesInSdp) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());
  SetFactoryDtlsSrtp();

  SendAudioOnlyStream2();
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);

  SessionDescriptionInterface* answer = CreateRemoteAnswer(offer);
  SetRemoteDescriptionWithoutError(answer);

  cricket::MediaSessionOptions options;
  options.recv_video = true;
  offer = CreateRemoteOffer(options, cricket::SEC_DISABLED);

  cricket::Candidate candidate1;
  candidate1.set_address(rtc::SocketAddress("1.1.1.1", 5000));
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName1, kMediaContentIndex1,
                                 candidate1);
  EXPECT_TRUE(offer->AddCandidate(&ice_candidate));
  SetRemoteDescriptionWithoutError(offer);

  answer = CreateAnswer(NULL);
  SetLocalDescriptionWithoutError(answer);
}

// Tests that we can renegotiate new media content with ICE candidates separated
// from the remote SDP.
TEST_P(WebRtcSessionTest, TestRenegotiateNewMediaWithCandidatesSeparated) {
  MAYBE_SKIP_TEST(rtc::SSLStreamAdapter::HaveDtlsSrtp);
  InitWithDtls(GetParam());
  SetFactoryDtlsSrtp();

  SendAudioOnlyStream2();
  SessionDescriptionInterface* offer = CreateOffer();
  SetLocalDescriptionWithoutError(offer);

  SessionDescriptionInterface* answer = CreateRemoteAnswer(offer);
  SetRemoteDescriptionWithoutError(answer);

  cricket::MediaSessionOptions options;
  options.recv_video = true;
  offer = CreateRemoteOffer(options, cricket::SEC_DISABLED);
  SetRemoteDescriptionWithoutError(offer);

  cricket::Candidate candidate1;
  candidate1.set_address(rtc::SocketAddress("1.1.1.1", 5000));
  candidate1.set_component(1);
  JsepIceCandidate ice_candidate(kMediaContentName1, kMediaContentIndex1,
                                 candidate1);
  EXPECT_TRUE(session_->ProcessIceMessage(&ice_candidate));

  answer = CreateAnswer(NULL);
  SetLocalDescriptionWithoutError(answer);
}
// Tests that RTX codec is removed from the answer when it isn't supported
// by local side.
TEST_F(WebRtcSessionTest, TestRtxRemovedByCreateAnswer) {
  Init();
  SendAudioVideoStream1();
  std::string offer_sdp(kSdpWithRtx);

  SessionDescriptionInterface* offer =
      CreateSessionDescription(JsepSessionDescription::kOffer, offer_sdp, NULL);
  EXPECT_TRUE(offer->ToString(&offer_sdp));

  // Offer SDP contains the RTX codec.
  EXPECT_TRUE(offer_sdp.find("rtx") != std::string::npos);
  SetRemoteDescriptionWithoutError(offer);

  SessionDescriptionInterface* answer = CreateAnswer(NULL);
  std::string answer_sdp;
  answer->ToString(&answer_sdp);
  // Answer SDP removes the unsupported RTX codec.
  EXPECT_TRUE(answer_sdp.find("rtx") == std::string::npos);
  SetLocalDescriptionWithoutError(answer);
}

// This verifies that the voice channel after bundle has both options from video
// and voice channels.
TEST_F(WebRtcSessionTest, TestSetSocketOptionBeforeBundle) {
  InitWithBundlePolicy(PeerConnectionInterface::kBundlePolicyBalanced);
  SendAudioVideoStream1();

  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.use_rtp_mux = true;

  SessionDescriptionInterface* offer = CreateOffer(options);
  SetLocalDescriptionWithoutError(offer);

  session_->video_channel()->SetOption(cricket::BaseChannel::ST_RTP,
                                       rtc::Socket::Option::OPT_SNDBUF, 4000);

  session_->voice_channel()->SetOption(cricket::BaseChannel::ST_RTP,
                                       rtc::Socket::Option::OPT_RCVBUF, 8000);

  int option_val;
  EXPECT_TRUE(session_->video_rtp_transport_channel()->GetOption(
      rtc::Socket::Option::OPT_SNDBUF, &option_val));
  EXPECT_EQ(4000, option_val);
  EXPECT_FALSE(session_->voice_rtp_transport_channel()->GetOption(
      rtc::Socket::Option::OPT_SNDBUF, &option_val));

  EXPECT_TRUE(session_->voice_rtp_transport_channel()->GetOption(
      rtc::Socket::Option::OPT_RCVBUF, &option_val));
  EXPECT_EQ(8000, option_val);
  EXPECT_FALSE(session_->video_rtp_transport_channel()->GetOption(
      rtc::Socket::Option::OPT_RCVBUF, &option_val));

  EXPECT_NE(session_->voice_rtp_transport_channel(),
            session_->video_rtp_transport_channel());

  SendAudioVideoStream2();
  SessionDescriptionInterface* answer =
      CreateRemoteAnswer(session_->local_description());
  SetRemoteDescriptionWithoutError(answer);

  EXPECT_TRUE(session_->voice_rtp_transport_channel()->GetOption(
      rtc::Socket::Option::OPT_SNDBUF, &option_val));
  EXPECT_EQ(4000, option_val);

  EXPECT_TRUE(session_->voice_rtp_transport_channel()->GetOption(
      rtc::Socket::Option::OPT_RCVBUF, &option_val));
  EXPECT_EQ(8000, option_val);
}

// Test creating a session, request multiple offers, destroy the session
// and make sure we got success/failure callbacks for all of the requests.
// Background: crbug.com/507307
TEST_F(WebRtcSessionTest, CreateOffersAndShutdown) {
  Init();

  rtc::scoped_refptr<WebRtcSessionCreateSDPObserverForTest> observers[100];
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
  cricket::MediaSessionOptions session_options;
  session_options.recv_audio = true;

  for (auto& o : observers) {
    o = new WebRtcSessionCreateSDPObserverForTest();
    session_->CreateOffer(o, options, session_options);
  }

  session_.reset();

  for (auto& o : observers) {
    // We expect to have received a notification now even if the session was
    // terminated.  The offer creation may or may not have succeeded, but we
    // must have received a notification which, so the only invalid state
    // is kInit.
    EXPECT_NE(WebRtcSessionCreateSDPObserverForTest::kInit, o->state());
  }
}

TEST_F(WebRtcSessionTest, TestPacketOptionsAndOnPacketSent) {
  TestPacketOptions();
}

// TODO(bemasc): Add a TestIceStatesBundle with BUNDLE enabled.  That test
// currently fails because upon disconnection and reconnection OnIceComplete is
// called more than once without returning to IceGatheringGathering.

INSTANTIATE_TEST_CASE_P(WebRtcSessionTests,
                        WebRtcSessionTest,
                        testing::Values(ALREADY_GENERATED,
                                        DTLS_IDENTITY_STORE));
