#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <exception>
#include "pybind11/pybind11.h"
#include "pybind11/numpy.h"
#include "jpeglib.h"

namespace py = pybind11;

#define VALID_DICOM true
#define INVALID_DICOM false
// dicom hexcodes: https://www.dabsoft.ch/dicom/6/6/
#define UNKNOWN_LENGTH 0xFFFFFFFF
#define ZERO_LENGTH 0x00000000
#define IMAGE_GROUP 0x0028
#define WIDTH_TAG 0x0010
#define HEIGHT_TAG 0x0011
#define PIXEL_DATA_GROUP 0x7FE0
#define PIXEL_DATA_TAG 0x0010
#define DELIM_A 0xFFFE
#define DELIM_B 0xE0DD

bool valid_dicom(FILE *c_file) {
  fseek(c_file, 128, SEEK_CUR);
  if (fgetc(c_file) == 'D' && fgetc(c_file) == 'I' &&
      fgetc(c_file) == 'C' && fgetc(c_file) == 'M') {
    return VALID_DICOM;
  } else {
    return INVALID_DICOM;
  }
}

struct Element {
  uint16_t group;
  uint16_t tag;
  char VR[2];
  u_int32_t VL;
};

void read_element(FILE *c_file, Element &element) {
  fread(&element.group, 2, 1, c_file);
  fread(&element.tag, 2, 1, c_file);
  fread(&element.VR, 1, 2, c_file);
  if (element.VR[0] == 'O' && element.VR[1] == 'B') {
    fseek(c_file, 2, SEEK_CUR);
    fread(&element.VL, 4, 1, c_file);
  } else {
    u_int16_t little_VL;
    fread(&little_VL, 2, 1, c_file);
    element.VL = little_VL;
  }
}

void print_element(Element &element) {
  std::cout << "[group, tag, VR, VL] = [" << std::hex << element.group << ", " << element.tag;
  std::cout << ", " << std::dec << element.VR[0] << element.VR[1] << ", " << element.VL << "]\n";
}

py::array_t<u_int8_t> read_dicom_image(
  std::string path,
  bool fast_and_lossy=false
) {
  FILE *c_file = fopen(path.c_str(), "rb");
  if (c_file == NULL) {
    fclose(c_file);
    py::print("Failed file open.");
    throw std::exception();
  }
  if (!valid_dicom(c_file)) {
    fclose(c_file);
    py::print("Invalid dicom file.");
    throw std::exception();
  }
  auto pixels = py::array_t<u_int8_t>(py::array::ShapeContainer({1024, 1024}));
  auto pixels_a = pixels.mutable_unchecked<2>();
  Element element;
  element.group = 77;
  element.tag = 77;
  element.VR[0] = '\0';
  element.VR[1] = '\0';
  element.VL = 0;
  while (!ferror(c_file) && !feof(c_file)) {
    read_element(c_file, element);
    if (element.VL == UNKNOWN_LENGTH) {
      if (element.group == PIXEL_DATA_GROUP && element.tag == PIXEL_DATA_TAG) {
        fseek(c_file, 16, SEEK_CUR);
        struct jpeg_decompress_struct jpeg_read;
        struct jpeg_error_mgr jpeg_err;
        jpeg_read.err = jpeg_std_error(&jpeg_err);
        jpeg_create_decompress(&jpeg_read);
        jpeg_stdio_src(&jpeg_read, c_file);
        jpeg_read_header(&jpeg_read, true);
        jpeg_read.dct_method = fast_and_lossy ? JDCT_FASTEST : JDCT_DEFAULT;
        jpeg_start_decompress(&jpeg_read);
        while (jpeg_read.output_scanline < jpeg_read.output_height) {
          size_t ptr_start = jpeg_read.output_scanline * jpeg_read.output_width;
          u_int8_t *row_ptr = pixels.mutable_data() + ptr_start;
          jpeg_read_scanlines(&jpeg_read, &row_ptr, 1);
        }
        jpeg_finish_decompress(&jpeg_read);
        jpeg_destroy_decompress(&jpeg_read);
        break;
      } else {
        fclose(c_file);
        py::print("Unsupported unknown length element.");
        throw std::exception();
      }
    } else {
      fseek(c_file, element.VL, SEEK_CUR);
    }
  }
  fclose(c_file);
  return pixels;
}

PYBIND11_MODULE(dicom_parse, m) {
  m.def(
    "read_dicom_image",
    &read_dicom_image,
    py::return_value_policy::take_ownership,
    py::arg("path"),
    py::arg("fast_and_lossy")=false
  );
}
