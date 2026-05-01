#ifndef RECORDINGTAP_H
#define RECORDINGTAP_H

#include <QIODevice>
#include <QVector>

QVector<double> recording_tap_compute_peaks(const QByteArray& chunk,
                                            int channel_count,
                                            int sample_size_bits);

class RecordingTap : public QIODevice {
  Q_OBJECT
 public:
  RecordingTap(QIODevice* underlying, int channel_count, int sample_size_bits,
               QObject* parent = nullptr);

 signals:
  void peaksAvailable(QVector<double> peaks);

 protected:
  qint64 readData(char*, qint64) override { return -1; }
  qint64 writeData(const char* data, qint64 len) override;

 private:
  QIODevice* underlying_;
  int channel_count_;
  int sample_size_bits_;
};

#endif  // RECORDINGTAP_H
