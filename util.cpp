#include "hdrmerge.h"
#include <ImfOutputFile.h>
#include <ImfInputFile.h>
#include <ImfChannelList.h>
#include <ImfStringAttribute.h>
#include <unistd.h>

void writeOpenEXR(const std::string &filename, size_t w, size_t h, int nChannels, float *data, const StringMap &metadata, bool writeHalf) {
	Imf::setGlobalThreadCount(getProcessorCount());

	Imf::Header header(w, h);
	for (StringMap::const_iterator it = metadata.begin(); it != metadata.end(); ++it)
		header.insert(it->first.c_str(), Imf::StringAttribute(it->second.c_str()));

	Imf::ChannelList &channels = header.channels();

	cout << "Writing " << filename << " .. " << endl;
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

int getProcessorCount() {
	return sysconf(_SC_NPROCESSORS_CONF);
}

int rawspeed_get_number_of_processor_cores() {
	return getProcessorCount();
}
