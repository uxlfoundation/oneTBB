/*
    Copyright (c) 2024 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#define NOMINMAX

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "../common/lodepng.h"
#include <tbb/tbb.h>

class PNGImage {
public:
  uint64_t frameNumber = -1;
  unsigned int width = 0, height = 0;
  std::shared_ptr<std::vector<unsigned char>> buffer; 
  static const int numChannels = 4; 
  static const int redOffset = 0; 
  static const int greenOffset = 1; 
  static const int blueOffset = 2; 

  PNGImage() {} 
  PNGImage(uint64_t frame_number, const std::string& file_name);
  PNGImage(const PNGImage& p);
  virtual ~PNGImage() {}
  void write() const;
};

int getNextFrameNumber();
PNGImage getLeftImage(uint64_t frameNumber);
PNGImage getRightImage(uint64_t frameNumber);
void increasePNGChannel(PNGImage& image, int channel_offset, int increase);
void mergePNGImages(PNGImage& right, const PNGImage& left);

void serial3DStereo() {
  while (uint64_t frameNumber = getNextFrameNumber()) {
    auto left = getLeftImage(frameNumber);
    auto right = getRightImage(frameNumber);
    increasePNGChannel(left, PNGImage::redOffset, 10);
    increasePNGChannel(right, PNGImage::blueOffset, 10);
    mergePNGImages(right, left);
    right.write();
  }
}

PNGImage::PNGImage(uint64_t frame_number, const std::string& file_name) :
  frameNumber{frame_number}, buffer{std::make_shared< std::vector<unsigned char> >()} {
  if (lodepng::decode(*buffer, width, height, file_name)) {
     std::cerr << "Error: could not read PNG file!" << std::endl;
     width = height = 0;
  }
};

PNGImage::PNGImage(const PNGImage& p) : frameNumber{p.frameNumber}, 
                                        width{p.width}, height{p.height},
                                        buffer{p.buffer} {}

void PNGImage::write() const {
  std::string file_name = std::string("out") + std::to_string(frameNumber) + ".png";
  if (lodepng::encode(file_name, *buffer, width, height)) {
    std::cerr << "Error: could not write PNG file!" << std::endl;
  }
}

static int stereo3DFrameCounter = 0;
static int stero3DNumImages = 0;

void initStereo3D(int num_images) {
  stereo3DFrameCounter = 0;
  stero3DNumImages = num_images;
}

int getNextFrameNumber() {
  if ( stereo3DFrameCounter < stero3DNumImages ) {
    return ++stereo3DFrameCounter;
  } else {
    return 0;
  }
}

PNGImage getLeftImage(uint64_t frameNumber) {
  return PNGImage(frameNumber, "input1.png");
}

PNGImage getRightImage(uint64_t frameNumber) {
  return PNGImage(frameNumber, "input2.png");
}

void increasePNGChannel(PNGImage& image, int channel_offset, int increase) {
  const int height_base = PNGImage::numChannels * image.width;
  std::vector<unsigned char>& buffer = *image.buffer;

  // Increase selected color channel by a predefined value
  for (unsigned int y = 0; y < image.height; y++) {
    const int height_offset = height_base * y;
    for (unsigned int x = 0; x < image.width; x++) {
      int pixel_offset = height_offset + PNGImage::numChannels * x + channel_offset;
      buffer[pixel_offset] = static_cast<uint8_t>(std::min(buffer[pixel_offset] + increase, 255));
    }
  }
}

void mergePNGImages(PNGImage& right, const PNGImage& left) {
  const int channels_per_pixel = PNGImage::numChannels;
  const int height_base = channels_per_pixel * right.width;
  std::vector<unsigned char>& left_buffer = *left.buffer;
  std::vector<unsigned char>& right_buffer = *right.buffer;

  for (unsigned int y = 0; y < right.height; y++) {
    const int height_offset = height_base * y;
    for (unsigned int x = 0; x < right.width; x++) {
      const int pixel_offset = height_offset + channels_per_pixel * x;
      const int red_index = pixel_offset + PNGImage::redOffset;
      right_buffer[red_index] = left_buffer[red_index];
    }
  }
}

int main() {
  int num_images = 3;

  initStereo3D(num_images);

  double serial_time = 0.0;
  {
    tbb::tick_count t0 = tbb::tick_count::now();
    serial3DStereo();
    serial_time = (tbb::tick_count::now() - t0).seconds();
  }
  std::cout << "serial_time == " << serial_time << " seconds" << std::endl;
  return 0;
}

