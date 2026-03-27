/***

    Olive - Non-Linear Video Editor
    Copyright (C) 2019  Olive Team

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "previewgenerator.h"

#include "ui/mediaiconservice.h"
#include "project/media.h"
#include "project/footage.h"
#include "panels/viewer.h"
#include "panels/project.h"
#include "global/config.h"
#include "core/path.h"
#include "global/debug.h"

#include <QPainter>
#include <QPixmap>
#include <QtMath>
#include <QTreeWidgetItem>
#include <QSemaphore>
#include <QFile>
#include <QDir>

QSemaphore sem(5); // only 5 preview generators can run at one time

PreviewGenerator::PreviewGenerator(Media* i) :
  QThread(nullptr)
{
  fmt_ctx_ = (nullptr);
  media_ = (i);
  retrieve_duration_ = (false);
  contains_still_image_ = (false);
  cancelled_ = (false);
  footage_ = media_->to_footage();

  footage_->preview_gen = this;

  data_dir_ = QDir(get_data_dir().filePath("previews"));
  if (!data_dir_.exists()) {
    data_dir_.mkpath(".");
  }

  connect(this, &QThread::finished, this, &QObject::deleteLater);

  // set up throbber animation
  amber::media_icon_service->SetMediaIcon(media_, ICON_TYPE_LOADING);

  start(QThread::LowPriority);
}

void PreviewGenerator::parse_media() {
  // detect video/audio streams in file
  for (int i=0;i<int(fmt_ctx_->nb_streams);i++) {
    // Find the decoder for the video stream
    if (avcodec_find_decoder(fmt_ctx_->streams[i]->codecpar->codec_id) == nullptr) {
      qCritical() << "Unsupported codec in stream" << i << "of file" << footage_->name;
    } else {
      FootageStream ms;
      ms.preview_done = false;
      ms.file_index = i;
      ms.enabled = true;
      ms.infinite_length = false;

      bool append = false;

      // Skip embedded cover art / thumbnails (e.g. DJI action cameras embed a JPG in MP4)
      if (fmt_ctx_->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) {
        continue;
      }

      if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
          && fmt_ctx_->streams[i]->codecpar->width > 0
          && fmt_ctx_->streams[i]->codecpar->height > 0) {

        // heuristic to determine if video is a still image (if it is, we treat it differently in the playback/render process)
        bool is_still_image = false;

        if (fmt_ctx_->streams[i]->avg_frame_rate.den == 0
            && fmt_ctx_->streams[i]->codecpar->codec_id != AV_CODEC_ID_DNXHD) {
          is_still_image = true;
        }

        // FFmpeg 6+ uses per-codec pipe demuxers (png_pipe, jpeg_pipe, etc.) instead of
        // the legacy "image2" demuxer. Check both to cover all FFmpeg versions.
        if (!is_still_image && fmt_ctx_->iformat != nullptr
            && !footage_->url.contains('%')) {
          QString fmt_name(fmt_ctx_->iformat->name);
          if (fmt_name.contains(QLatin1String("image2")) || fmt_name.endsWith(QLatin1String("_pipe"))) {
            is_still_image = true;
          }
        }

        if (is_still_image) {
          if (footage_->url.contains('%')) {
            // must be an image sequence
            ms.video_frame_rate = 25;
          } else {
            ms.infinite_length = true;
            contains_still_image_ = true;
            ms.video_frame_rate = 0;
          }

        } else {
          // using ffmpeg's built-in heuristic
          ms.video_frame_rate = av_q2d(av_guess_frame_rate(fmt_ctx_, fmt_ctx_->streams[i], nullptr));
        }

        ms.video_width = fmt_ctx_->streams[i]->codecpar->width;
        ms.video_height = fmt_ctx_->streams[i]->codecpar->height;

        // default value, we get the true value later in generate_waveform()
        ms.video_auto_interlacing = VIDEO_PROGRESSIVE;
        ms.video_interlacing = VIDEO_PROGRESSIVE;

        append = true;
      } else if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        ms.audio_channels = fmt_ctx_->streams[i]->codecpar->ch_layout.nb_channels;
        {
          uint64_t mask = 0;
          // Try to extract a channel mask; if ch_layout isn't mask-based, fall back to default
          if (fmt_ctx_->streams[i]->codecpar->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
            mask = fmt_ctx_->streams[i]->codecpar->ch_layout.u.mask;
          } else {
            AVChannelLayout temp = {};
            av_channel_layout_default(&temp, ms.audio_channels);
            if (temp.order == AV_CHANNEL_ORDER_NATIVE) mask = temp.u.mask;
            av_channel_layout_uninit(&temp);
          }
          ms.audio_layout = int(mask);
        }
        ms.audio_frequency = fmt_ctx_->streams[i]->codecpar->sample_rate;

        append = true;
      }

      if (append) {
        QVector<FootageStream>& stream_list = (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ?
              footage_->audio_tracks : footage_->video_tracks;

        for (auto & j : stream_list) {
          if (j.file_index == i) {
            j = ms;
            append = false;
          }
        }

        if (append) stream_list.append(ms);
      }
    }
  }
  footage_->length = fmt_ctx_->duration;

  if (fmt_ctx_->duration == INT64_MIN) {
    retrieve_duration_ = true;
  } else {
    finalize_media();
  }
}

bool PreviewGenerator::retrieve_preview(const QString& hash) {
  // returns true if generate_waveform must be run, false if we got all previews from cached files
  if (retrieve_duration_) {
    //dout << "[NOTE] " << media->name << "needs to retrieve duration";
    return true;
  }

  bool found = true;
  for (auto & ms : footage_->video_tracks) {
    QString thumb_path = get_thumbnail_path(hash, ms);
    QFile f(thumb_path);
    if (f.exists() && ms.video_preview.load(thumb_path)) {
      ms.preview_done = true;
    } else {
      found = false;
      break;
    }
  }
  for (auto & ms : footage_->audio_tracks) {
    QString waveform_path = get_waveform_path(hash, ms);
    QFile f(waveform_path);
    if (f.exists()) {
      //dout << "loaded wave" << ms->file_index << "from" << waveform_path;
      if (f.open(QFile::ReadOnly)) {
        QByteArray data = f.readAll();
        ms.audio_preview.resize(data.size());
        for (int j=0;j<data.size();j++) {
          // faster way?
          ms.audio_preview[j] = data.at(j);
        }
        ms.preview_done = true;
        f.close();
      }
    } else {
      found = false;
      break;
    }
  }
  if (!found) {
    for (auto & ms : footage_->video_tracks) {
      ms.preview_done = false;
    }
    for (auto & ms : footage_->audio_tracks) {
      ms.audio_preview.clear();
      ms.preview_done = false;
    }
  }
  return !found;
}

void PreviewGenerator::finalize_media() {
  if (!cancelled_) {
    bool footage_is_ready = true;

    if (footage_->video_tracks.isEmpty() && footage_->audio_tracks.isEmpty()) {
      // ERROR
      footage_is_ready = false;
      invalidate_media(tr("Failed to find any valid video/audio streams"));
    } else if (!footage_->video_tracks.isEmpty() && !contains_still_image_) {
      // VIDEO
      amber::media_icon_service->SetMediaIcon(media_, ICON_TYPE_VIDEO);
    } else if (!footage_->audio_tracks.isEmpty()) {
      // AUDIO
      amber::media_icon_service->SetMediaIcon(media_, ICON_TYPE_AUDIO);
    } else {
      // IMAGE
      amber::media_icon_service->SetMediaIcon(media_, ICON_TYPE_IMAGE);
    }

    if (footage_is_ready) {
      footage_->ready_lock.unlock();
      footage_->ready = true;
      media_->update_tooltip();
    }

    if (amber::ActiveSequence != nullptr) {
      amber::ActiveSequence->RefreshClips(media_);
    }
  }
}

void PreviewGenerator::invalidate_media(const QString &error_msg)
{
  media_->update_tooltip(error_msg);
  amber::media_icon_service->SetMediaIcon(media_, ICON_TYPE_ERROR);
  footage_->invalid = true;
  footage_->ready_lock.unlock();
}

bool PreviewGenerator::setup_stream_codecs(AVCodecContext** codec_ctx, qint16*** waveform_cache_data) {
  if (!codec_ctx) {
    qWarning() << "setup_stream_codecs: codec_ctx is null";
    return false;
  }
  if (!waveform_cache_data) {
    qWarning() << "setup_stream_codecs: waveform_cache_data is null";
    return false;
  }
  bool create_previews = false;

  for (unsigned int i=0;i<fmt_ctx_->nb_streams;i++) {

    // default to nullptr values for easier memory management later
    codec_ctx[i] = nullptr;
    waveform_cache_data[i] = nullptr;

    // we only generate previews for video and audio
    // and only if the thumbnail and waveform sizes are > 0
    if ((fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && amber::CurrentConfig.thumbnail_resolution > 0)
        || (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && amber::CurrentConfig.waveform_resolution > 0)) {
      const AVCodec* codec = avcodec_find_decoder(fmt_ctx_->streams[i]->codecpar->codec_id);
      if (codec != nullptr) {

        // alloc the context and load the params into it
        codec_ctx[i] = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_ctx[i], fmt_ctx_->streams[i]->codecpar);

        // open the decoder
        avcodec_open2(codec_ctx[i], codec, nullptr);

        // audio specific functions
        if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {

          // allocate sample cache for this stream
          waveform_cache_data[i] = new qint16* [fmt_ctx_->streams[i]->codecpar->ch_layout.nb_channels];

          // each channel gets a min and a max value so we allocate two ints for each one
          for (int j=0;j<fmt_ctx_->streams[i]->codecpar->ch_layout.nb_channels;j++) {
            waveform_cache_data[i][j] = new qint16[2];
          }

          // if codec context has no defined channel layout, guess it from the channel count
          if (codec_ctx[i]->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
            av_channel_layout_default(&codec_ctx[i]->ch_layout, fmt_ctx_->streams[i]->codecpar->ch_layout.nb_channels);
          }

        }

        // enable next step of process
        create_previews = true;
      }
    }
  }

  return create_previews;
}

void PreviewGenerator::process_video_frame(AVFrame* temp_frame, FootageStream* s,
                                           AVCodecContext** codec_ctx, int stream_index) {
  if (!temp_frame) {
    qWarning() << "process_video_frame: temp_frame is null";
    return;
  }
  if (!s) {
    qWarning() << "process_video_frame: s is null";
    return;
  }
  if (!codec_ctx) {
    qWarning() << "process_video_frame: codec_ctx is null";
    return;
  }
  if (s->preview_done) return;

  int dstH = amber::CurrentConfig.thumbnail_resolution;
  int dstW = qRound(dstH * (float(temp_frame->width)/float(temp_frame->height)));

  SwsContext* sws_ctx = sws_getContext(
        temp_frame->width,
        temp_frame->height,
        static_cast<AVPixelFormat>(temp_frame->format),
        dstW,
        dstH,
        static_cast<AVPixelFormat>(AV_PIX_FMT_RGBA),
        SWS_FAST_BILINEAR,
        nullptr,
        nullptr,
        nullptr
        );

  int linesize[AV_NUM_DATA_POINTERS];
  linesize[0] = dstW*4;

  s->video_preview = QImage(dstW, dstH, QImage::Format_RGBA8888);
  uint8_t* data = s->video_preview.bits();

  sws_scale(sws_ctx,
            temp_frame->data,
            temp_frame->linesize,
            0,
            temp_frame->height,
            &data,
            linesize);

  // is video interlaced?
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 29, 100)
  s->video_auto_interlacing = (temp_frame->flags & AV_FRAME_FLAG_INTERLACED) ? ((temp_frame->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ? VIDEO_TOP_FIELD_FIRST : VIDEO_BOTTOM_FIELD_FIRST) : VIDEO_PROGRESSIVE;
#else
  s->video_auto_interlacing = temp_frame->interlaced_frame ? (temp_frame->top_field_first ? VIDEO_TOP_FIELD_FIRST : VIDEO_BOTTOM_FIELD_FIRST) : VIDEO_PROGRESSIVE;
#endif
  s->video_interlacing = s->video_auto_interlacing;

  s->preview_done = true;

  sws_freeContext(sws_ctx);

  if (!retrieve_duration_) {
    avcodec_free_context(&codec_ctx[stream_index]);
    codec_ctx[stream_index] = nullptr;
  }
}

void PreviewGenerator::process_audio_frame(AVFrame* temp_frame, FootageStream* s, int stream_index,
                                           qint16*** waveform_cache_data, int& waveform_cache_count,
                                           AVPacket* packet) {
  if (!temp_frame) {
    qWarning() << "process_audio_frame: temp_frame is null";
    return;
  }
  if (!s) {
    qWarning() << "process_audio_frame: s is null";
    return;
  }
  if (!waveform_cache_data) {
    qWarning() << "process_audio_frame: waveform_cache_data is null";
    return;
  }
  AVFrame* swr_frame = av_frame_alloc();
  av_channel_layout_copy(&swr_frame->ch_layout, &temp_frame->ch_layout);
  swr_frame->sample_rate = temp_frame->sample_rate;
  swr_frame->format = AV_SAMPLE_FMT_S16P;

  SwrContext* swr_ctx = nullptr;
  swr_alloc_set_opts2(
        &swr_ctx,
        &temp_frame->ch_layout,
        static_cast<AVSampleFormat>(swr_frame->format),
        temp_frame->sample_rate,
        &temp_frame->ch_layout,
        static_cast<AVSampleFormat>(temp_frame->format),
        temp_frame->sample_rate,
        0,
        nullptr
        );

  swr_init(swr_ctx);

  swr_convert_frame(swr_ctx, swr_frame, temp_frame);

  // `config.waveform_resolution` determines how many samples per second are stored in waveform.
  // `sample_rate` is samples per second, so `interval` is how many samples are averaged in
  // each "point" of the waveform
  int interval = qFloor((temp_frame->sample_rate/amber::CurrentConfig.waveform_resolution)/4)*4;

  // get the amount of bytes in an audio sample
  int sample_size = av_get_bytes_per_sample(static_cast<AVSampleFormat>(swr_frame->format));

  // total amount of data in this frame
  int nb_bytes = swr_frame->nb_samples * sample_size;

  // loop through entire frame
  for (int i=0;i<nb_bytes;i+=sample_size) {

    // check if we've hit sample threshold
    if (waveform_cache_count == interval) {

      // if so, we dump our cached values into the preview and reset them
      // for the next interval
      for (int j=0;j<swr_frame->ch_layout.nb_channels;j++) {
        qint16& min = waveform_cache_data[stream_index][j][0];
        qint16& max = waveform_cache_data[stream_index][j][1];

        s->audio_preview.append(min >> 8);
        s->audio_preview.append(max >> 8);
      }

      waveform_cache_count = 0;
    }

    // standard processing for each channel of information
    for (int j=0;j<swr_frame->ch_layout.nb_channels;j++) {
      qint16& min = waveform_cache_data[stream_index][j][0];
      qint16& max = waveform_cache_data[stream_index][j][1];

      // if we're starting over, reset cache to zero
      if (waveform_cache_count == 0) {
        min = 0;
        max = 0;
      }

      // store most minimum and most maximum samples of this interval
      qint16 sample = qint16((swr_frame->data[j][i+1] << 8) | swr_frame->data[j][i]);
      min = qMin(min, sample);
      max = qMax(max, sample);
    }

    waveform_cache_count++;

    if (cancelled_) {
      break;
    }
  }

  swr_free(&swr_ctx);
  av_frame_free(&swr_frame);
}

void PreviewGenerator::retrieve_media_duration(int64_t* media_lengths) {
  if (!media_lengths) {
    qWarning() << "retrieve_media_duration: media_lengths is null";
    return;
  }
  footage_->length = 0;
  unsigned int maximum_stream = 0;
  for (unsigned int i=0;i<fmt_ctx_->nb_streams;i++) {
    if (media_lengths[i] > media_lengths[maximum_stream]) {
      maximum_stream = i;
    }
  }

  // FIXME: length is currently retrieved as a frame count rather than a timestamp
  AVRational afr = fmt_ctx_->streams[maximum_stream]->avg_frame_rate;
  if (afr.num != 0 && afr.den != 0) {
    footage_->length = qRound(double(media_lengths[maximum_stream]) / av_q2d(afr) * AV_TIME_BASE);
  } else {
    AVRational rfr = fmt_ctx_->streams[maximum_stream]->r_frame_rate;
    if (rfr.num != 0 && rfr.den != 0) {
      footage_->length = qRound(double(media_lengths[maximum_stream]) / av_q2d(rfr) * AV_TIME_BASE);
    } else if (fmt_ctx_->streams[maximum_stream]->duration > 0) {
      double time_base = av_q2d(fmt_ctx_->streams[maximum_stream]->time_base);
      footage_->length = qRound(double(fmt_ctx_->streams[maximum_stream]->duration) * time_base * AV_TIME_BASE);
    }
  }

  finalize_media();
}

void PreviewGenerator::generate_waveform() {
  AVFrame* temp_frame = av_frame_alloc();

  // stores codec contexts for format's streams
  AVCodecContext** codec_ctx = new AVCodecContext* [fmt_ctx_->nb_streams];

  // stores media lengths while scanning in case the format has no duration metadata
  int64_t* media_lengths = new int64_t[fmt_ctx_->nb_streams]{0};

  // stores samples while scanning before they get sent to preview file
  qint16*** waveform_cache_data = new qint16** [fmt_ctx_->nb_streams];
  int waveform_cache_count = 0;

  bool create_previews = setup_stream_codecs(codec_ctx, waveform_cache_data);

  if (create_previews) {
    // TODO may be unnecessary - doesn't av_read_frame allocate a packet itself?
    AVPacket* packet = av_packet_alloc();

    bool done = true;

    bool end_of_file = false;

    // get the ball rolling
    int initial_ret;
    do {
      initial_ret = av_read_frame(fmt_ctx_, packet);
      if (initial_ret < 0) break;
    } while (codec_ctx[packet->stream_index] == nullptr);
    if (initial_ret < 0) {
      end_of_file = true;
    } else {
      avcodec_send_packet(codec_ctx[packet->stream_index], packet);
    }

    while (!end_of_file) {
      while (codec_ctx[packet->stream_index] == nullptr || avcodec_receive_frame(codec_ctx[packet->stream_index], temp_frame) == AVERROR(EAGAIN)) {
        av_packet_unref(packet);
        int read_ret = av_read_frame(fmt_ctx_, packet);

        if (read_ret < 0) {
          end_of_file = true;
          if (read_ret != AVERROR_EOF) qCritical() << "Failed to read packet for preview generation" << read_ret;
          break;
        }
        if (codec_ctx[packet->stream_index] != nullptr) {
          int send_ret = avcodec_send_packet(codec_ctx[packet->stream_index], packet);
          if (send_ret < 0 && send_ret != AVERROR(EAGAIN)) {
            qCritical() << "Failed to send packet for preview generation - aborting" << send_ret;
            end_of_file = true;
            break;
          }
        }
      }
      if (!end_of_file) {
        FootageStream* s = footage_->get_stream_from_file_index(fmt_ctx_->streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO, packet->stream_index);
        if (s != nullptr) {
          if (fmt_ctx_->streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            process_video_frame(temp_frame, s, codec_ctx, packet->stream_index);
            media_lengths[packet->stream_index]++;
          } else if (fmt_ctx_->streams[packet->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            process_audio_frame(temp_frame, s, packet->stream_index,
                                waveform_cache_data, waveform_cache_count, packet);

            if (cancelled_) {
              end_of_file = true;
              break;
            }
          }
        }

        // check if we've got all our previews
        if (retrieve_duration_) {
          done = false;
        } else if (footage_->audio_tracks.size() == 0) {
          done = true;
          for (const auto & video_track : footage_->video_tracks) {
            if (!video_track.preview_done) {
              done = false;
              break;
            }
          }
          if (done) {
            end_of_file = true;
            break;
          }
        }
        av_packet_unref(packet);
      }
    }

    av_packet_free(&packet);

    for (unsigned int i=0;i<fmt_ctx_->nb_streams;i++) {
      if (waveform_cache_data[i] != nullptr && codec_ctx[i] != nullptr) {
        for (int j=0;j<codec_ctx[i]->ch_layout.nb_channels;j++) {
          delete [] waveform_cache_data[i][j];
        }
        delete [] waveform_cache_data[i];
      }

      if (codec_ctx[i] != nullptr) {
        avcodec_free_context(&codec_ctx[i]);
      }
    }

    // by this point, we'll have made all audio waveform previews
    for (auto & audio_track : footage_->audio_tracks) {
      audio_track.preview_done = true;
    }
  }

  av_frame_free(&temp_frame);

  if (retrieve_duration_) {
    retrieve_media_duration(media_lengths);
  }

  delete [] waveform_cache_data;
  delete [] media_lengths;
  delete [] codec_ctx;
}

QString PreviewGenerator::get_thumbnail_path(const QString& hash, const FootageStream& ms) {
  return data_dir_.filePath(QString("%1t%2").arg(hash, QString::number(ms.file_index)));
}

QString PreviewGenerator::get_waveform_path(const QString& hash, const FootageStream& ms) {
  return data_dir_.filePath(QString("%1w%2").arg(hash, QString::number(ms.file_index)));
}

void PreviewGenerator::run() {
  Q_ASSERT(footage_ != nullptr);
  Q_ASSERT(media_ != nullptr);

  const QString url = footage_->url;
  QByteArray ba = url.toUtf8();
  char* filename = qstrdup(ba.constData());

  QString errorStr;
  bool error = false;

  AVDictionary* format_opts = nullptr;

  // for image sequences that don't start at 0, set the index where it does start
  if (footage_->start_number > 0) {
    av_dict_set(&format_opts, "start_number", QString::number(footage_->start_number).toUtf8(), 0);
  }

  int errCode = avformat_open_input(&fmt_ctx_, filename, nullptr, &format_opts);
  if(errCode != 0) {
    char err[1024];
    av_strerror(errCode, err, 1024);
    errorStr = tr("Could not open file - %1").arg(err);
    error = true;
  } else {
    errCode = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (errCode < 0) {
      char err[1024];
      av_strerror(errCode, err, 1024);
      errorStr = tr("Could not find stream information - %1").arg(err);
      error = true;
    } else {
      av_dump_format(fmt_ctx_, 0, filename, 0);
      parse_media();

      // see if we already have data for this
      QString hash = get_file_hash(footage_->url);

      if (retrieve_preview(hash)) {
        sem.acquire();

        if (!cancelled_) {
          generate_waveform();

          if (!cancelled_) {
            // save preview to file
            for (auto & ms : footage_->video_tracks) {
              ms.video_preview.save(get_thumbnail_path(hash, ms), "PNG");
              //dout << "saved" << ms->file_index << "thumbnail to" << get_thumbnail_path(hash, ms);
            }
            for (auto & ms : footage_->audio_tracks) {
              QFile f(get_waveform_path(hash, ms));
              if (f.open(QFile::WriteOnly)) {
                f.write(ms.audio_preview.constData(), ms.audio_preview.size());
                f.close();
              }
              //dout << "saved" << ms->file_index << "waveform to" << get_waveform_path(hash, ms);
            }
          }
        }

        sem.release();
      }
    }
    avformat_close_input(&fmt_ctx_);
  }

  if (!cancelled_) {
    if (error) {
      invalidate_media(errorStr);
    }
  }

  delete [] filename;
  footage_->preview_gen = nullptr;
}

void PreviewGenerator::cancel() {
  cancelled_ = true;
  wait();
}

void PreviewGenerator::AnalyzeMedia(Media *m)
{
  // PreviewGenerator's constructor starts the thread, sets a reference of itself as the media's generator,
  // and connects its thread completion to its own deletion, therefore handling its own memory. Nothing else needs to
  // be done.
  new PreviewGenerator(m);
}
