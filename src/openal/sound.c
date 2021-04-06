/* Copyright (C) 2020 SovietPony
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License ONLY.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sound.h"
#include "music.h"

#include "files.h" // F_findres F_getreslen
#include "memory.h" // M_lock M_unlock
#include "error.h" // logo

#include "common/endianness.h"

#ifdef __APPLE__
#  include <OpenAL/al.h>
#  include <OpenAL/alc.h>
#else
#  include <AL/al.h>
#  include <AL/alc.h>
#endif

#include "SDL.h" // SDL_BuildAudioCVT SDL_ConvertAudio
#include <assert.h>
#include <stdlib.h> // malloc
#include <string.h> // memcpy

#define TAG_OAL1 0x4F414C31
#define MAX_CHANNELS 8

#pragma pack(1)
typedef struct dmi {
  dword len;
  dword rate;
  dword lstart;
  dword llen;
  byte data[];
} dmi;
#pragma pack()

typedef struct openal_snd {
  snd_t base;
  ALuint buffer;
} openal_snd;

typedef struct openal_channel {
  ALuint source;
} openal_channel;

static short snd_vol;

static ALCdevice *device;
static ALCcontext *context;
static ALuint sources[MAX_CHANNELS];

/* Music */

const cfg_t *MUS_args (void) {
  return NULL;
}

const cfg_t *MUS_conf (void) {
  return NULL;
}

const menu_t *MUS_menu (void) {
  return NULL;
}

void MUS_init (void) {

}

void MUS_done (void) {

}

void MUS_start (int time) {

}

void MUS_stop (void) {

}

void MUS_volume (int v) {

}

void MUS_load (char n[8]) {

}

void MUS_free (void) {

}

void MUS_update (void) {

}

/* Sound */

static int sound_menu_handler (menu_msg_t *msg, const menu_t *m, int i) {
  static int cur;
  enum { VOLUME, __NUM__ };
  static const simple_menu_t sm = {
    GM_BIG, "Sound", NULL,
    {
      { "Volume", NULL },
    }
  };
  if (i == VOLUME) {
    switch (msg->type) {
      case GM_GETENTRY: return GM_init_int0(msg, GM_SCROLLER, 0, 0, 0);
      case GM_GETINT: return GM_init_int(msg, snd_vol, 0, 128, 8);
      case GM_SETINT: S_volume(msg->integer.i); return 1;
    }
  }
  return simple_menu_handler(msg, i, __NUM__, &sm, &cur);
}

const menu_t *S_menu (void) {
  static const menu_t m = { &sound_menu_handler };
  return &m;
}

const cfg_t *S_args (void) {
  static const cfg_t args[] = {
    { "sndvol", &snd_vol, Y_WORD },
    { NULL, NULL, 0 }
  };
  return args;
}

const cfg_t *S_conf (void) {
  static const cfg_t conf[] = {
    { "sound_volume", &snd_vol, Y_WORD },
    { NULL, NULL, 0 }
  };
  return conf;
}

static void convert_this_ext (Uint32 src_format, int src_chan, int src_rate, Uint32 dst_format, int dst_chan, int dst_rate, const void *buf, int len, void **maxbuf, int *maxlen) {
  SDL_AudioCVT cvt;
  *maxlen = 0;
  *maxbuf = NULL;
  if (SDL_BuildAudioCVT(&cvt, src_format, src_chan, src_rate, dst_format, dst_chan, dst_rate) != -1) {
    *maxlen = len * cvt.len_mult;
    *maxbuf = malloc(*maxlen);
    if (*maxbuf != NULL) {
      memcpy(*maxbuf, buf, len);
      cvt.buf = *maxbuf;
      cvt.len = len;
      if (SDL_ConvertAudio(&cvt) == 0) {
        *maxlen = len * cvt.len_ratio;
      } else {
        free(*maxbuf);
        *maxbuf = NULL;
        *maxlen = 0;
      }
    }
  }
}

static openal_snd *new_openal_snd (const void *data, dword len, dword rate, dword lstart, dword llen, int sign) {
  assert(data);
  ALuint buffer = 0;
  openal_snd *snd = NULL;
  void *newdata = NULL;
  int newlen = 0;
  // for some reason 8bit formats makes psshshshsh
  // TODO do this without SDL
  convert_this_ext(sign ? AUDIO_S8 : AUDIO_U8, 1, rate, AUDIO_S16SYS, 1, rate, data, len, &newdata, &newlen);
  if (newdata != NULL) {
    alGenBuffers(1, &buffer);
    if (alGetError() == AL_NO_ERROR) {
      alBufferData(buffer, AL_FORMAT_MONO16, newdata, newlen, rate);
      if (alGetError() == AL_NO_ERROR) {
        snd = malloc(sizeof(openal_snd));
        if (snd != NULL) {
          snd->base.tag = TAG_OAL1;
          snd->buffer = buffer;
          // TODO loops
        } else {
          alDeleteBuffers(1, &buffer);
        }
      } else {
        alDeleteBuffers(1, &buffer);
      }
    }
    free(newdata);
  }
  return snd;
}

snd_t *S_get (int id) {
  void *handle;
  openal_snd *snd = NULL;
  if (context != NULL) {
    handle = M_lock(id);
    if (handle != NULL) {
      void *data = handle;
      dword len = F_getreslen(id);
      dword rate = 11025;
      dword lstart = 0;
      dword llen = 0;
      int sign = 0;
      if (len > 16) {
        dmi *hdr = handle;
        dword hdr_len = int2host(hdr->len);
        dword hdr_rate = int2host(hdr->rate);
        dword hdr_lstart = int2host(hdr->lstart);
        dword hdr_llen = int2host(hdr->llen);
        if (hdr_len <= len - 8 && hdr_lstart + hdr_llen <= len - 16) {
          data = hdr->data;
          len = hdr_len;
          rate = hdr_rate;
          lstart = hdr_lstart;
          llen = hdr_llen;
          sign = 1;
        }
      }
      snd = new_openal_snd(data, len, rate, lstart, llen, sign);
      M_unlock(handle);
    }
  }
  return (snd_t*)snd;
}

snd_t *S_load (const char name[8]) {
  return S_get(F_findres(name));
}

void S_free (snd_t *s) {
  int i;
  ALint h;
  openal_snd *snd = (openal_snd*)s;
  if (snd != NULL) {
    assert(snd->base.tag == TAG_OAL1);
    if (context != NULL) {
      for (i = 0; i < MAX_CHANNELS; i++) {
        alGetSourcei(sources[i], AL_BUFFER, &h);
        if (h == snd->buffer) {
          alSourceStop(sources[i]);
          alSourcei(sources[i], AL_BUFFER, 0);
        }
      }
      alDeleteBuffers(1, &snd->buffer);
      assert(alGetError() == AL_NO_ERROR);
    }
    snd->base.tag = 0;
    free(s);
  }
}

void S_init (void) {
  assert(device == NULL && context == NULL);
  const ALCint attrs[] = {ALC_MONO_SOURCES, MAX_CHANNELS, 0};
  device = alcOpenDevice(NULL);
  if (device != NULL) {
    context = alcCreateContext(device, attrs);
    if (context != NULL) { 
      if (alcMakeContextCurrent(context)) {
        alGenSources(MAX_CHANNELS, sources);
        if (alGetError() == AL_NO_ERROR) {
          alListenerf(AL_GAIN, 1);
          alListener3f(AL_POSITION, 0, 0, 0);
          alListener3f(AL_VELOCITY, 0, 0, 0);
        } else {
          logo("S_init: unable to create OpenAL sources\n");
          alcDestroyContext(context);
          alcCloseDevice(device);
          context = NULL;
          device = NULL;
        }
      } else {
        logo("S_init: unable to make OpenAL context current\n");
        alcDestroyContext(context);
        alcCloseDevice(device);
        context = NULL;
        device = NULL;
      }
    } else {
      logo("S_init: unable to create OpenAL context\n");
      alcCloseDevice(device);
      device = NULL;
    }
  } else {
    logo("S_init: unable to open OpenAL device\n");
  }
}

void S_done (void) {
  if (context != NULL) {
    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);
  }
  context = NULL;
  device = NULL;
}

short S_play (snd_t *s, short c, short v) {
  assert(c >= 0 && c <= MAX_CHANNELS);
  assert(v >= 0 && v < 256);
  ALuint source;
  ALint state;
  int channel;
  if (context != NULL && s != NULL) {
    openal_snd *snd = (openal_snd*)s;
    assert(snd->base.tag == TAG_OAL1);
    if (c == 0) {
      for (channel = 0; channel < MAX_CHANNELS; channel++) {
        state = AL_PLAYING;
        alGetSourcei(sources[channel], AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED || state == AL_INITIAL) {
          break; // !!!
        }
      }
    } else {
      channel = c - 1;
    }
    if (channel < MAX_CHANNELS) {
      source = sources[channel];
      alSourceStop(source);
      alSourcei(source, AL_BUFFER, snd->buffer);
      alSourcef(source, AL_PITCH, 1);
      alSourcef(source, AL_GAIN, v / 255.0);
      alSource3f(source, AL_POSITION, 0, 0, 0);
      alSource3f(source, AL_VELOCITY, 0, 0, 0);
      alSourcei(source, AL_LOOPING, AL_FALSE);
      alSourcePlay(source);
    }
  }
  return 0;
}

void S_stop (short c) {
  assert(c >= 0 && c <= MAX_CHANNELS);
  if (context != NULL) {
    if (c != 0) {
      alSourceStop(sources[c - 1]);
    }
  }
}

void S_volume (int v) {
  assert(v >= 0 && v <= 128);
  snd_vol = v;
  if (context != NULL) {
    alListenerf(AL_GAIN, v / 128.0);
  }
}

void S_wait (void) {
  int i;
  ALint state;
  if (context != NULL) {
    for (i = 0; i < MAX_CHANNELS; i++) {
      do {
        state = AL_STOPPED;
        alGetSourcei(sources[i], AL_SOURCE_STATE, &state);
      } while (state == AL_PLAYING);
    }
  }
}
