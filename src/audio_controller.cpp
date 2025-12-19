// Copyright (c) 2009-2010, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include "audio_controller.h"

#include "audio_timing.h"
#include "include/aegisub/audio_player.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "project.h"

#include <libaegisub/audio/provider.h>
#include <libaegisub/log.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <memory>
#include <vector>

#ifdef WITH_SOUNDTOUCH
#include <SoundTouch.h>
#endif

class AudioController::SpeedProvider final : public agi::AudioProvider {
	agi::AudioProvider *source = nullptr;
	std::atomic<double> speed{1.0};
	std::atomic<double> sample_offset{0.0};
	std::atomic<bool> keep_pitch{true};

#ifdef WITH_SOUNDTOUCH
	class SoundTouchTimeStretch {
		soundtouch::SoundTouch st;
		int sample_rate = 0;
		int channels = 0;

	public:
		void Configure(int sr, int ch, double new_speed) {
			sample_rate = sr;
			channels = ch;
			st.setSampleRate(sample_rate);
			st.setChannels(channels);
			st.setPitch(1.0f);
			st.setRate(1.0f);
			st.setTempo(static_cast<float>(new_speed));
		}

		void Clear() { st.clear(); }
		void Flush() { st.flush(); }
		int Receive(float *out, int frames) { return st.receiveSamples(out, frames); }
		void Put(const float *in, int frames) { st.putSamples(in, frames); }
	};

	mutable SoundTouchTimeStretch stretch;
#endif

	mutable std::mutex stretch_mutex;
	mutable int64_t stream_output_pos = 0;
	mutable int64_t stream_source_pos = 0;
	mutable bool stream_flushed = false;

	mutable std::vector<float> float_buffer;
	mutable std::vector<float> stretch_output;
	mutable std::vector<int16_t> int_buffer;

	static std::atomic<bool> warned_soundtouch_missing;

	static int16_t FloatToInt16(float sample) {
		float clamped = std::max(-1.0f, std::min(1.0f, sample));
		int value = static_cast<int>(std::lround(clamped * 32767.0f));
		if (value < -0x8000) value = -0x8000;
		else if (value > 0x7FFF) value = 0x7FFF;
		return static_cast<int16_t>(value);
	}

	static float Int16ToFloat(int16_t sample) {
		return static_cast<float>(sample) / 32768.0f;
	}

	static constexpr int kChunkFrames = 2048;

	void ResetStreamLocked(double cur_speed, double cur_offset, int64_t start) const {
		stream_output_pos = start;
		double start_pos = std::isfinite(cur_offset) ? cur_offset + start * cur_speed : 0.0;
		if (!std::isfinite(start_pos))
			start_pos = 0.0;
		stream_source_pos = static_cast<int64_t>(std::llround(start_pos));
		stream_flushed = false;
#ifdef WITH_SOUNDTOUCH
		stretch.Clear();
		stretch.Configure(sample_rate, channels, cur_speed);
#endif
	}

	void FillLegacy(void *buf, int64_t start, int64_t count, double cur_speed, double cur_offset) const {
		if (channels <= 0 || count <= 0) {
			ZeroFill(buf, count);
			return;
		}

		const double src_first_pos = cur_offset + start * cur_speed;
		const double src_last_pos = cur_offset + (start + count - 1) * cur_speed;

		int64_t src_start = static_cast<int64_t>(std::floor(src_first_pos));
		int64_t src_end = static_cast<int64_t>(std::floor(src_last_pos)) + 2;
		int64_t src_count = src_end - src_start;
		if (src_count <= 0) {
			ZeroFill(buf, count);
			return;
		}

		if (float_samples && bytes_per_sample == sizeof(float)) {
			std::vector<float> src(static_cast<size_t>(src_count * channels));
			source->GetAudio(src.data(), src_start, src_count);

			auto *dst = static_cast<float *>(buf);
			for (int64_t i = 0; i < count; ++i) {
				const double src_pos = cur_offset + (start + i) * cur_speed;
				const int64_t base = static_cast<int64_t>(std::floor(src_pos)) - src_start;
				const double frac = src_pos - std::floor(src_pos);

				for (int ch = 0; ch < channels; ++ch) {
					float s0 = 0.0f, s1 = 0.0f;
					if (base >= 0 && base < src_count)
						s0 = src[static_cast<size_t>((base * channels) + ch)];
					if ((base + 1) >= 0 && (base + 1) < src_count)
						s1 = src[static_cast<size_t>(((base + 1) * channels) + ch)];

					dst[static_cast<size_t>(i * channels + ch)] = static_cast<float>((1.0 - frac) * s0 + frac * s1);
				}
			}
			return;
		}

		if (bytes_per_sample != static_cast<int>(sizeof(int16_t))) {
			ZeroFill(buf, count);
			return;
		}

		std::vector<int16_t> src(static_cast<size_t>(src_count * channels));
		source->GetAudio(src.data(), src_start, src_count);

		auto *dst = static_cast<int16_t *>(buf);
		for (int64_t i = 0; i < count; ++i) {
			const double src_pos = cur_offset + (start + i) * cur_speed;
			const int64_t base = static_cast<int64_t>(std::floor(src_pos)) - src_start;
			const double frac = src_pos - std::floor(src_pos);

			for (int ch = 0; ch < channels; ++ch) {
				const int64_t idx0 = base * channels + ch;
				const int64_t idx1 = (base + 1) * channels + ch;

				int s0 = (idx0 >= 0 && idx0 < static_cast<int64_t>(src.size())) ? src[static_cast<size_t>(idx0)] : 0;
				int s1 = (idx1 >= 0 && idx1 < static_cast<int64_t>(src.size())) ? src[static_cast<size_t>(idx1)] : 0;

				int value = static_cast<int>(std::lround((1.0 - frac) * s0 + frac * s1));
				value = std::clamp(value, -0x8000, 0x7FFF);
				dst[static_cast<size_t>(i * channels + ch)] = static_cast<int16_t>(value);
			}
		}
	}

	int64_t FillWithSoundTouch(void *buf, int64_t start, int64_t count, double cur_speed, double cur_offset) const {
#ifdef WITH_SOUNDTOUCH
		if (channels <= 0) {
			ZeroFill(buf, count);
			return 0;
		}

		std::lock_guard<std::mutex> lock(stretch_mutex);
		stretch.Configure(sample_rate, channels, cur_speed);

		if (start != stream_output_pos)
			ResetStreamLocked(cur_speed, cur_offset, start);

		int64_t produced = 0;
		const int64_t source_limit = source ? source->GetNumSamples() : 0;

		while (produced < count) {
			const int frames_needed = static_cast<int>(std::min<int64_t>(kChunkFrames, count - produced));
			int got = 0;

			if (float_samples && bytes_per_sample == sizeof(float)) {
				got = stretch.Receive(static_cast<float *>(buf) + static_cast<size_t>(produced * channels), frames_needed);
			}
			else {
				stretch_output.resize(static_cast<size_t>(frames_needed * channels));
				got = stretch.Receive(stretch_output.data(), frames_needed);
				if (got > 0) {
					auto *dst = static_cast<int16_t *>(buf) + static_cast<size_t>(produced * channels);
					for (int i = 0; i < got * channels; ++i)
						dst[static_cast<size_t>(i)] = FloatToInt16(stretch_output[static_cast<size_t>(i)]);
				}
			}

			if (got > 0) {
				produced += got;
				continue;
			}

			if (!stream_flushed && stream_source_pos >= source_limit) {
				stretch.Flush();
				stream_flushed = true;
				continue;
			}

			if (stream_flushed && stream_source_pos >= source_limit) {
				if (float_samples && bytes_per_sample == sizeof(float)) {
					std::fill_n(static_cast<float *>(buf) + static_cast<size_t>(produced * channels), static_cast<size_t>((count - produced) * channels), 0.0f);
				}
				else {
					std::fill_n(static_cast<int16_t *>(buf) + static_cast<size_t>(produced * channels), static_cast<size_t>((count - produced) * channels), 0);
				}
				produced = count;
				break;
			}

			const int64_t available = std::max<int64_t>(source_limit - stream_source_pos, 0);
			if (available <= 0) {
				stream_flushed = true;
				continue;
			}

			int frames_to_read = static_cast<int>(std::min<int64_t>(available, std::max<int64_t>(frames_needed * static_cast<int64_t>(std::ceil(std::max(cur_speed, 1.0 / std::max(cur_speed, 1e-6)))), static_cast<int64_t>(kChunkFrames))));
			if (frames_to_read <= 0)
				frames_to_read = static_cast<int>(std::min<int64_t>(available, static_cast<int64_t>(kChunkFrames)));

			float_buffer.resize(static_cast<size_t>(frames_to_read * channels));

			if (source->AreSamplesFloat()) {
				source->GetAudio(float_buffer.data(), stream_source_pos, frames_to_read);
			}
			else {
				int_buffer.resize(static_cast<size_t>(frames_to_read * channels));
				source->GetAudio(int_buffer.data(), stream_source_pos, frames_to_read);
				for (size_t i = 0; i < float_buffer.size(); ++i)
					float_buffer[i] = Int16ToFloat(int_buffer[i]);
			}

			stretch.Put(float_buffer.data(), frames_to_read);
			stream_source_pos += frames_to_read;
		}

		stream_output_pos = start + produced;
		return produced;
#else
		ZeroFill(buf, count);
		return 0;
#endif
	}

	bool UseSoundTouch(double cur_speed) const {
#ifdef WITH_SOUNDTOUCH
		return keep_pitch.load(std::memory_order_relaxed) && std::abs(cur_speed - 1.0) >= 1e-9;
#else
		(void)cur_speed;
		return false;
#endif
	}

public:
	explicit SpeedProvider(agi::AudioProvider *src) : source(src) {
		SetSource(src);
	}

	void SetSource(agi::AudioProvider *src) {
		source = src;
		if (source) {
			channels = source->GetChannels();
			bytes_per_sample = source->GetBytesPerSample();
			float_samples = source->AreSamplesFloat();
			sample_rate = source->GetSampleRate();
			num_samples = source->GetNumSamples();
		}
		else {
			channels = 1;
			bytes_per_sample = static_cast<int>(sizeof(int16_t));
			float_samples = false;
			sample_rate = 0;
			num_samples = 0;
		}
		decoded_samples = num_samples;
		speed.store(1.0, std::memory_order_relaxed);
		sample_offset.store(0.0, std::memory_order_relaxed);
		ResetStream();
	}

	void SetSpeed(double new_speed) {
		if (!std::isfinite(new_speed) || new_speed <= 0.0)
			new_speed = 1.0;
		speed.store(new_speed, std::memory_order_relaxed);

		if (!source) return;

		// The output stream gets longer when playing slower than 1.0x.
		double slowdown = std::min(new_speed, 1.0);
		int64_t required = source->GetNumSamples();
		if (slowdown < 1.0)
			required = static_cast<int64_t>(std::ceil(source->GetNumSamples() / slowdown));

		if (required > num_samples)
			num_samples = required;
		decoded_samples = num_samples;
	}

	void SetSampleOffset(double new_offset) {
		if (!std::isfinite(new_offset))
			new_offset = 0.0;
		sample_offset.store(new_offset, std::memory_order_relaxed);
	}

	void SetKeepPitch(bool enable) {
		keep_pitch.store(enable, std::memory_order_relaxed);
#if !defined(WITH_SOUNDTOUCH)
		if (enable && !warned_soundtouch_missing.exchange(true)) {
			LOG_W("audio/controller") << "SoundTouch not available; falling back to legacy speed playback without pitch preservation.";
		}
#endif
	}

	void ResetStream() {
		std::lock_guard<std::mutex> lock(stretch_mutex);
		const double cur_speed = speed.load(std::memory_order_relaxed);
		const double cur_offset = sample_offset.load(std::memory_order_relaxed);
		ResetStreamLocked(cur_speed, cur_offset, 0);
	}

protected:
	void FillBuffer(void *buf, int64_t start, int64_t count) const override {
		if (!source || count <= 0) {
			ZeroFill(buf, count);
			return;
		}

		const double cur_speed = speed.load(std::memory_order_relaxed);
		const double cur_offset = sample_offset.load(std::memory_order_relaxed);

		if (!std::isfinite(cur_speed) || cur_speed <= 0.0 || !std::isfinite(cur_offset)) {
			ZeroFill(buf, count);
			return;
		}

		if (UseSoundTouch(cur_speed)) {
			FillWithSoundTouch(buf, start, count, cur_speed, cur_offset);
			return;
		}

		FillLegacy(buf, start, count, cur_speed, cur_offset);
	}
};

std::atomic<bool> AudioController::SpeedProvider::warned_soundtouch_missing{false};

void AudioController::EnsureAudioPlayerForSpeed(double speed) {
	if (!provider) return;

	if (!std::isfinite(speed) || speed <= 0.0)
		speed = 1.0;

	// Hard bypass at 1.0x: use the exact pre-playback-speed audio pipeline
	// (player fed from the original provider) to avoid quality regressions.
	const bool want_speed_provider = std::abs(speed - 1.0) >= 1e-9;
	if (player && player_uses_speed_provider == want_speed_provider)
		return;

	if (playback_mode != PM_NotPlaying)
		Stop();
	else if (player)
		player->Stop();

	player.reset();
	player_uses_speed_provider = want_speed_provider;

	try {
		if (want_speed_provider) {
			if (!speed_provider)
				speed_provider = std::make_unique<SpeedProvider>(provider);
			else
				speed_provider->SetSource(provider);

			player = AudioPlayerFactory::GetAudioPlayer(speed_provider.get(), context->parent);
		}
		else {
			player = AudioPlayerFactory::GetAudioPlayer(provider, context->parent);
		}
	}
	catch (...) {
		player.reset();
		player_uses_speed_provider = false;
		context->project->CloseAudio();
	}

	AnnounceAudioPlayerOpened();
}

AudioController::AudioController(agi::Context *context)
: context(context)
, playback_timer(this)
, provider_connection(context->project->AddAudioProviderListener(&AudioController::OnAudioProvider, this))
{
	Bind(wxEVT_TIMER, &AudioController::OnPlaybackTimer, this, playback_timer.GetId());

#ifdef wxHAS_POWER_EVENTS
	Bind(wxEVT_POWER_SUSPENDED, &AudioController::OnComputerSuspending, this);
	Bind(wxEVT_POWER_RESUME, &AudioController::OnComputerResuming, this);
#endif

	OPT_SUB("Audio/Player", &AudioController::OnAudioPlayerChanged, this);
}

AudioController::~AudioController()
{
	Stop();
}

void AudioController::OnPlaybackTimer(wxTimerEvent &)
{
	if (!player) return;

	int64_t pos = player->GetCurrentPosition();
	if (!player->IsPlaying() ||
		(playback_mode != PM_ToEnd && pos >= player->GetEndPosition()+200))
	{
		// The +200 is to allow the player to end the sound output cleanly,
		// otherwise a popping artifact can sometimes be heard.
		Stop();
	}
	else
	{
		AnnouncePlaybackPosition(MillisecondsFromSamples(pos));
	}
}

#ifdef wxHAS_POWER_EVENTS
void AudioController::OnComputerSuspending(wxPowerEvent &)
{
	Stop();
	player.reset();
	player_uses_speed_provider = false;
}

void AudioController::OnComputerResuming(wxPowerEvent &)
{
	OnAudioPlayerChanged();
}
#endif

void AudioController::OnAudioPlayerChanged()
{
	if (!provider) return;

	Stop();
	player.reset();
	player_uses_speed_provider = false;

	try
	{
		player = AudioPlayerFactory::GetAudioPlayer(provider, context->parent);
	}
	catch (...)
	{
		/// @todo This really shouldn't be just swallowing all audio player open errors
		context->project->CloseAudio();
	}
	AnnounceAudioPlayerOpened();
}

void AudioController::OnAudioProvider(agi::AudioProvider *new_provider)
{
	provider = new_provider;
	Stop();
	player.reset();
	player_uses_speed_provider = false;
	speed_provider.reset();
	OnAudioPlayerChanged();
}

void AudioController::SetTimingController(std::unique_ptr<AudioTimingController> new_controller)
{
	timing_controller = std::move(new_controller);
	if (timing_controller)
		timing_controller->AddUpdatedPrimaryRangeListener(&AudioController::OnTimingControllerUpdatedPrimaryRange, this);

	AnnounceTimingControllerChanged();
}

void AudioController::OnTimingControllerUpdatedPrimaryRange()
{
	if (playback_mode == PM_PrimaryRange)
		player->SetEndPosition(SamplesFromMilliseconds(timing_controller->GetPrimaryPlaybackRange().end()));
}

void AudioController::PlayRange(const TimeRange &range)
{
	EnsureAudioPlayerForSpeed(1.0);
	if (!player) return;

	playback_speed = 1.0;
	playback_sample_offset = 0.0;

	player->Play(SamplesFromMilliseconds(range.begin()), SamplesFromMilliseconds(range.length()));
	playback_mode = PM_Range;
	playback_timer.Start(20);

	AnnouncePlaybackPosition(range.begin());
}

void AudioController::PlayRange(const TimeRange &range, double speed)
{
	if (!std::isfinite(speed) || speed <= 0.0)
		speed = 1.0;
	if (std::abs(speed - 1.0) < 1e-9) {
		PlayRange(range);
		return;
	}

	EnsureAudioPlayerForSpeed(speed);
	if (!player || !speed_provider) return;

	int64_t start_sample = SamplesFromMilliseconds(range.begin());
	int64_t sample_count = SamplesFromMilliseconds(range.length());
	if (sample_count <= 0) return;

	const bool keep_pitch = OPT_GET("Audio/Keep Pitch When Changing Speed")->GetBool();

	playback_speed = speed;
	playback_sample_offset = static_cast<double>(start_sample);
	speed_provider->SetSpeed(speed);
	speed_provider->SetSampleOffset(playback_sample_offset);
	speed_provider->SetKeepPitch(keep_pitch);
	speed_provider->ResetStream();

	auto out_count = static_cast<int64_t>(std::ceil(sample_count / speed));
	player->Play(0, out_count);
	playback_mode = PM_Range;
	playback_timer.Start(20);

	AnnouncePlaybackPosition(range.begin());
}

void AudioController::PlayPrimaryRange()
{
	PlayRange(GetPrimaryPlaybackRange());
	if (playback_mode == PM_Range)
		playback_mode = PM_PrimaryRange;
}

void AudioController::PlayToEndOfPrimary(int start_ms)
{
	PlayRange(TimeRange(start_ms, GetPrimaryPlaybackRange().end()));
	if (playback_mode == PM_Range)
		playback_mode = PM_PrimaryRange;
}

void AudioController::PlayToEnd(int start_ms)
{
	EnsureAudioPlayerForSpeed(1.0);
	if (!player || !provider) return;

	playback_speed = 1.0;
	playback_sample_offset = 0.0;

	int64_t start_sample = SamplesFromMilliseconds(start_ms);
	player->Play(start_sample, provider->GetNumSamples()-start_sample);
	playback_mode = PM_ToEnd;
	playback_timer.Start(20);

	AnnouncePlaybackPosition(start_ms);
}

void AudioController::PlayToEnd(int start_ms, double speed)
{
	if (!std::isfinite(speed) || speed <= 0.0)
		speed = 1.0;
	if (std::abs(speed - 1.0) < 1e-9) {
		PlayToEnd(start_ms);
		return;
	}

	EnsureAudioPlayerForSpeed(speed);
	if (!player || !speed_provider || !provider) return;

	int64_t start_sample = SamplesFromMilliseconds(start_ms);
	int64_t sample_count = provider->GetNumSamples() - start_sample;
	if (sample_count <= 0) return;

	const bool keep_pitch = OPT_GET("Audio/Keep Pitch When Changing Speed")->GetBool();

	playback_speed = speed;
	playback_sample_offset = static_cast<double>(start_sample);
	speed_provider->SetSpeed(speed);
	speed_provider->SetSampleOffset(playback_sample_offset);
	speed_provider->SetKeepPitch(keep_pitch);
	speed_provider->ResetStream();

	auto out_count = static_cast<int64_t>(std::ceil(sample_count / speed));
	player->Play(0, out_count);
	playback_mode = PM_ToEnd;
	playback_timer.Start(20);

	AnnouncePlaybackPosition(start_ms);
}

void AudioController::Stop()
{
	if (!player) return;

	player->Stop();
	playback_mode = PM_NotPlaying;
	playback_timer.Stop();
	if (speed_provider)
		speed_provider->ResetStream();

	AnnouncePlaybackStop();
}

bool AudioController::IsPlaying()
{
	return player && playback_mode != PM_NotPlaying;
}

int AudioController::GetPlaybackPosition()
{
	if (!IsPlaying()) return 0;

	return MillisecondsFromSamples(player->GetCurrentPosition());
}

int AudioController::GetDuration() const
{
	if (!provider) return 0;
	return (provider->GetNumSamples() * 1000 + provider->GetSampleRate() - 1) / provider->GetSampleRate();
}

TimeRange AudioController::GetPrimaryPlaybackRange() const
{
	if (timing_controller)
		return timing_controller->GetPrimaryPlaybackRange();
	else
		return TimeRange{0, 0};
}

void AudioController::SetVolume(double volume)
{
	if (!player) return;
	player->SetVolume(volume);
}

int64_t AudioController::SamplesFromMilliseconds(int64_t ms) const
{
	if (!provider) return 0;
	return (ms * provider->GetSampleRate() + 999) / 1000;
}

int64_t AudioController::MillisecondsFromSamples(int64_t samples) const
{
	if (!provider) return 0;
	double speed = playback_speed;
	if (!std::isfinite(speed) || speed <= 0.0)
		speed = 1.0;

	double src_samples = playback_sample_offset + samples * speed;
	if (!std::isfinite(src_samples))
		return 0;

	return static_cast<int64_t>(src_samples * 1000.0 / provider->GetSampleRate());
}
