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

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

class AudioController::SpeedProvider final : public agi::AudioProvider {
	agi::AudioProvider *source = nullptr;
	std::atomic<double> speed{1.0};
	std::atomic<double> sample_offset{0.0};

public:
	explicit SpeedProvider(agi::AudioProvider *src) : source(src) {
		channels = 1;
		bytes_per_sample = sizeof(int16_t);
		float_samples = false;
		sample_rate = source ? source->GetSampleRate() : 0;
		num_samples = source ? source->GetNumSamples() : 0;
		decoded_samples = num_samples;
	}

	void SetSource(agi::AudioProvider *src) {
		source = src;
		sample_rate = source ? source->GetSampleRate() : 0;
		num_samples = source ? source->GetNumSamples() : 0;
		decoded_samples = num_samples;
		speed.store(1.0, std::memory_order_relaxed);
		sample_offset.store(0.0, std::memory_order_relaxed);
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

		const double src_first_pos = cur_offset + start * cur_speed;
		const double src_last_pos = cur_offset + (start + count - 1) * cur_speed;

		int64_t src_start = static_cast<int64_t>(std::floor(src_first_pos));
		int64_t src_end = static_cast<int64_t>(std::floor(src_last_pos)) + 2;
		int64_t src_count = src_end - src_start;
		if (src_count <= 0) {
			ZeroFill(buf, count);
			return;
		}

		std::vector<int16_t> src(static_cast<size_t>(src_count));
		source->GetInt16MonoAudio(src.data(), src_start, src_count);

		auto *dst = static_cast<int16_t *>(buf);
		for (int64_t i = 0; i < count; ++i) {
			const double src_pos = cur_offset + (start + i) * cur_speed;
			int64_t idx0 = static_cast<int64_t>(std::floor(src_pos)) - src_start;
			double frac = src_pos - std::floor(src_pos);

			// idx0 is guaranteed in-range due to src_start/src_end computation and clamping in GetInt16MonoAudio
			int s0 = idx0 >= 0 && idx0 < src_count ? src[static_cast<size_t>(idx0)] : 0;
			int s1 = (idx0 + 1) >= 0 && (idx0 + 1) < src_count ? src[static_cast<size_t>(idx0 + 1)] : 0;

			int value = static_cast<int>(std::lround((1.0 - frac) * s0 + frac * s1));
			if (value < -0x8000) value = -0x8000;
			else if (value > 0x7FFF) value = 0x7FFF;
			dst[i] = static_cast<int16_t>(value);
		}
	}
};

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

	try
	{
		if (!speed_provider)
			speed_provider = std::make_unique<SpeedProvider>(provider);
		player = AudioPlayerFactory::GetAudioPlayer(speed_provider.get(), context->parent);
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
	speed_provider.reset();
	if (provider)
		speed_provider = std::make_unique<SpeedProvider>(provider);
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
	if (!player || !speed_provider) return;

	playback_speed = 1.0;
	playback_sample_offset = 0.0;
	speed_provider->SetSpeed(1.0);
	speed_provider->SetSampleOffset(0.0);

	player->Play(SamplesFromMilliseconds(range.begin()), SamplesFromMilliseconds(range.length()));
	playback_mode = PM_Range;
	playback_timer.Start(20);

	AnnouncePlaybackPosition(range.begin());
}

void AudioController::PlayRange(const TimeRange &range, double speed)
{
	if (!player || !speed_provider) return;
	if (!std::isfinite(speed) || speed <= 0.0)
		speed = 1.0;
	if (std::abs(speed - 1.0) < 1e-9) {
		PlayRange(range);
		return;
	}

	int64_t start_sample = SamplesFromMilliseconds(range.begin());
	int64_t sample_count = SamplesFromMilliseconds(range.length());
	if (sample_count <= 0) return;

	playback_speed = speed;
	playback_sample_offset = static_cast<double>(start_sample);
	speed_provider->SetSpeed(speed);
	speed_provider->SetSampleOffset(playback_sample_offset);

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
	if (!player || !speed_provider) return;

	playback_speed = 1.0;
	playback_sample_offset = 0.0;
	speed_provider->SetSpeed(1.0);
	speed_provider->SetSampleOffset(0.0);

	int64_t start_sample = SamplesFromMilliseconds(start_ms);
	player->Play(start_sample, provider->GetNumSamples()-start_sample);
	playback_mode = PM_ToEnd;
	playback_timer.Start(20);

	AnnouncePlaybackPosition(start_ms);
}

void AudioController::PlayToEnd(int start_ms, double speed)
{
	if (!player || !speed_provider) return;
	if (!std::isfinite(speed) || speed <= 0.0)
		speed = 1.0;
	if (std::abs(speed - 1.0) < 1e-9) {
		PlayToEnd(start_ms);
		return;
	}

	int64_t start_sample = SamplesFromMilliseconds(start_ms);
	int64_t sample_count = provider->GetNumSamples() - start_sample;
	if (sample_count <= 0) return;

	playback_speed = speed;
	playback_sample_offset = static_cast<double>(start_sample);
	speed_provider->SetSpeed(speed);
	speed_provider->SetSampleOffset(playback_sample_offset);

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
