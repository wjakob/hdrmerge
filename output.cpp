#include "hdrmerge.h"

#include <boost/format.hpp>

#include <ImfOutputFile.h>
#include <ImfChannelList.h>
#include <ImfStringAttribute.h>

extern "C" {
	#include <jpeglib.h>
	#include <jerror.h>
};

void writeOpenEXR(const std::string &filename, size_t w, size_t h, int nChannels, float *data, const StringMap &metadata, bool writeHalf) {
	Imf::setGlobalThreadCount(getProcessorCount());

	Imf::Header header(w, h);
	for (StringMap::const_iterator it = metadata.begin(); it != metadata.end(); ++it)
		header.insert(it->first.c_str(), Imf::StringAttribute(it->second.c_str()));

	Imf::ChannelList &channels = header.channels();

	cout << "Writing " << filename << " (" << w << "x" << h << ", " << nChannels
		 << " channels, " << (writeHalf ? "half" : "single") << " precision) .. " << endl;
	if (nChannels == 3) {
		if (writeHalf) {
			channels.insert("R", Imf::Channel(Imf::HALF));
			channels.insert("G", Imf::Channel(Imf::HALF));
			channels.insert("B", Imf::Channel(Imf::HALF));

			/* Though it would be nicer to do the conversion scanline by scanline,
			   this would prevent us from using OpenEXR's multithreading abilities.
			   Hence, convert everything at once with a full-sized buffer */
			half *buffer = new half[3*w*h];
			for (size_t j=0; j<3*w*h; ++j)
				buffer[j] = *data++;

			Imf::FrameBuffer frameBuffer;
			frameBuffer.insert("R", Imf::Slice(Imf::HALF, (char *) buffer,   6, 6*w));
			frameBuffer.insert("G", Imf::Slice(Imf::HALF, (char *) buffer+2, 6, 6*w));
			frameBuffer.insert("B", Imf::Slice(Imf::HALF, (char *) buffer+4, 6, 6*w));

			Imf::OutputFile file(filename.c_str(), header);
			file.setFrameBuffer(frameBuffer);
			file.writePixels(h);
			delete[] buffer;
		} else {
			channels.insert("R", Imf::Channel(Imf::FLOAT));
			channels.insert("G", Imf::Channel(Imf::FLOAT));
			channels.insert("B", Imf::Channel(Imf::FLOAT));
			Imf::FrameBuffer frameBuffer;
			frameBuffer.insert("R", Imf::Slice(Imf::FLOAT, (char *) data,   12, 12*w));
			frameBuffer.insert("G", Imf::Slice(Imf::FLOAT, (char *) data+4, 12, 12*w));
			frameBuffer.insert("B", Imf::Slice(Imf::FLOAT, (char *) data+8, 12, 12*w));
			Imf::OutputFile file(filename.c_str(), header);
			file.setFrameBuffer(frameBuffer);
			file.writePixels(h);
		}
	} else if (nChannels == 1) {
		if (writeHalf) {
			channels.insert("Y", Imf::Channel(Imf::HALF));

			/* Though it would be nicer to do the conversion scanline by scanline,
			   this would prevent us from using OpenEXR's multithreading abilities.
			   Hence, convert everything at once with a full-sized buffer */
			half *buffer = new half[w*h];
			for (size_t j=0; j<w*h; ++j)
				buffer[j] = *data++;

			Imf::FrameBuffer frameBuffer;
			frameBuffer.insert("Y", Imf::Slice(Imf::HALF, (char *) buffer,   2, 2*w));

			Imf::OutputFile file(filename.c_str(), header);
			file.setFrameBuffer(frameBuffer);
			file.writePixels(h);
			delete[] buffer;
		} else {
			channels.insert("Y", Imf::Channel(Imf::FLOAT));
			Imf::FrameBuffer frameBuffer;
			frameBuffer.insert("Y", Imf::Slice(Imf::FLOAT, (char *) data,   4, 4*w));
			Imf::OutputFile file(filename.c_str(), header);
			file.setFrameBuffer(frameBuffer);
			file.writePixels(h);
		}
	} else {
		throw std::runtime_error("writeOpenEXR(): unknown number of channels!");
	}
}

extern "C" {
	METHODDEF(void) jpeg_error_exit (j_common_ptr cinfo) throw(std::runtime_error) {
		char msg[JMSG_LENGTH_MAX];
		(*cinfo->err->format_message) (cinfo, msg);
		throw std::runtime_error((boost::format("Critcal libjpeg error: %1%") % msg).str());
	}
};

void writeJPEG(const std::string &filename, size_t w, size_t h, float *data, int quality) {
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	FILE *file = fopen(filename.c_str(), "w");
	if (!file)
		throw std::runtime_error("Unable to open output file");

	cout << "Writing " << filename << " (" << w << "x" << h << ", "
		 << "3 channels, low dynamic range) .. " << endl;

	cinfo.err = jpeg_std_error(&jerr);
	jerr.error_exit = jpeg_error_exit;
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, file);

	cinfo.image_width = (int) w;
	cinfo.image_height = (int) h;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	uint8_t *buffer = new uint8_t[w * h * 3];
	uint8_t **scanlines = new uint8_t*[h];

	#pragma omp parallel for
	for (int i=0; i<h; ++i) {
		float *in_ptr = data + w * i * 3;
		uint8_t *out_ptr = buffer + w * i * 3;
		scanlines[i] = out_ptr;

		for (int j=0; j<3*w; ++j) {
			float value = *in_ptr++;
			if (value <= 0.0031308f)
				value = 12.92f * value;
			else
				value = 1.055f * std::pow(value, 1.0f/2.4f) - 0.055f;

			*out_ptr ++ = (uint8_t) std::max(std::min(255.0f, std::round(value * 255.0f)), 0.0f);
		}
	}
	jpeg_write_scanlines(&cinfo, scanlines, (int) h);

	/* Release the libjpeg data structures */
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	delete[] buffer;
	delete[] scanlines;
	fclose(file);
}

