// SynchFromEnd.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#pragma warning(disable : 4996) //_CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <chrono>   // hours, minutes, duration_cast
#include <algorithm>

extern "C"
{
	#include "libavformat/avformat.h"
	#include "libavutil/frame.h"
	#include "libavutil/avutil.h"
	#include "libavutil/display.h"
	#include "libavutil/mathematics.h"
	
	//#include "libavutil/stereo3d.h"
}

using namespace std;

double get_rotation(AVStream *st) {
	uint8_t* displaymatrix = av_stream_get_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
	double theta = 0;
	if (displaymatrix)
		theta = -av_display_rotation_get((int32_t*)displaymatrix);

	theta -= 360 * floor(theta / 360 + 0.9 / 360);

	if (fabs(theta - 90 * round(theta / 90)) > 2)
		av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n");

	return round(theta);
}

class VideoInfos {
	public:
		int64_t width;
		int64_t height;
		int64_t duration_us;
		int64_t start_time_realtime_us;
		int64_t nb_frames;
		double frame_rate;
		double rotation;

		void printInfos() {
			std::cout << "Size = " << width << 'x' << height << "\nDuration = " << duration_us << "us\nNumber of frames = " << nb_frames << "\nfps = " << frame_rate << "\n";
		}
};

class Crop {
	public:
		int64_t width;
		int64_t height;
		int64_t x;
		int64_t y;

		Crop() : width(0), height(0), x(0), y(0) {}

		Crop(int64_t w, int64_t h, int64_t _x, int64_t _y) : width(w), height(h), x(_x), y(_y) {}

		int ApplyCrop(double cfx, double cfy) {
			int64_t new_width = width * (1.0 - abs(cfx)/100.0);
			int64_t new_height = height * (1.0 - abs(cfy)/100.0);
			
			if (cfx > 0)
				x = width - new_width;
			if (cfy > 0)
				y = height - new_height;

			width = new_width;
			height = new_height;

			return 1;
		}

};

class Video {
	public:
		const char *path;
		int64_t trim_time;
		Crop crop_values;

		Video() {
			path = new char[100];
			trim_time = 0;
		}

		Video(const Video &_vid) {
			Video(_vid.path);
			trim_time = _vid.trim_time;
		}

		Video(const char *_path) {
			path = _path;
			trim_time = 0;
			int VideoStreamIndex = -1;
			AVFormatContext* pFormatCtx = avformat_alloc_context();
			avformat_open_input(&pFormatCtx, path, NULL, NULL);
			for (unsigned int i = 0; i < pFormatCtx->nb_streams; i++)
			{
				if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
					/* if video stream found then get the index */
				{
					VideoStreamIndex = i;
					break;
				}
			}

			/* if video stream not availabe */
			if ((VideoStreamIndex) == -1)
			{
				std::cout << "video streams not found" << std::endl;
			}

			infos.duration_us = pFormatCtx->duration;
			infos.frame_rate = av_q2d(pFormatCtx->streams[VideoStreamIndex]->avg_frame_rate);
			infos.nb_frames = pFormatCtx->streams[VideoStreamIndex]->nb_frames;
			infos.width = pFormatCtx->streams[VideoStreamIndex]->codec->width;
			infos.height = pFormatCtx->streams[VideoStreamIndex]->codec->height;
			infos.start_time_realtime_us = pFormatCtx->start_time_realtime;
			infos.rotation = get_rotation(pFormatCtx->streams[VideoStreamIndex]);
						
			avformat_close_input(&pFormatCtx);
			avformat_free_context(pFormatCtx);

			crop_values = Crop(infos.width, infos.height, 0, 0);
		}

		void switch_width_height() {
			int64_t new_width = infos.height;
			infos.height = infos.width;
			infos.width = new_width;
		}

		VideoInfos getInfos() {
			return infos;
		}
		void printInfos() {
			std::cout << "Path = " << path << "\n";
			infos.printInfos();
		}
	private:
		VideoInfos infos;
};

class VideoPair {
	private:
		int crop(double cfx, double cfy) {
			L.crop_values.ApplyCrop(cfx, cfy);
			R.crop_values.ApplyCrop(-cfx, -cfy);
			return 1;
		}

	public:
		Video L;
		Video R;

		VideoPair(const char* _path_L, const char* _path_R) {
			L = Video(_path_L);
			R = Video(_path_R);
		}

		VideoPair(const Video &_L, const Video &_R) : L(_L), R(_R) {}

		//int trim(const char* output) {
		//	return trim(output);
		//}

		int trim(const char* output, double cfx=0.0, double cfy=0.0, double fpsmax=60.0, int64_t res_reduction_factor=1) {
			float framerate = min(min(L.getInfos().frame_rate, R.getInfos().frame_rate), fpsmax);

			if ((int)round(L.getInfos().rotation) % 180)
				L.switch_width_height();
			if ((int)round(R.getInfos().rotation) % 180)
				R.switch_width_height();

			int64_t width = min(L.getInfos().width, R.getInfos().width) / res_reduction_factor;
			int64_t height = min(L.getInfos().height, R.getInfos().height) / res_reduction_factor;

			return trim(framerate, width, height, cfx, cfy, output);
		}

		int trim(float framerate, int64_t width, int64_t height, double cfx, double cfy, const char *output) {
			int64_t duration = min(L.getInfos().duration_us, R.getInfos().duration_us); //- (int64_t)(1000000/min(L.frame_rate,R.frame_rate));
			int64_t start_trim = abs(L.getInfos().duration_us - R.getInfos().duration_us);

			if (L.getInfos().duration_us == duration) {
				R.trim_time = start_trim;
			}
			else {
				L.trim_time = start_trim;
			}

			int x1 = 0;
			int x2 = 0;
			int y1 = 0;
			int y2 = 0;

			if (cfx > 0)
				x1 = width * (abs(cfx) / 100.0);
			else
				x2 = width * (abs(cfx) / 100.0);

			if (cfy > 0)
				y1 = height * (abs(cfy) / 100.0);
			else
				y2 = height * (abs(cfy) / 100.0);

			int w = width * (1 - abs(cfx) / 100.0);
			int h = height * (1.0 - abs(cfy) / 100.0);

			char *ffmpeg_command = new char[350 + strlen(L.path) + strlen(R.path) + strlen(output)];
			sprintf(ffmpeg_command, "ffmpeg -y -i %s -i %s -filter_complex \"[0:v]trim=start=%lldus,scale=%i:%i,fps=%f,setpts=PTS-STARTPTS,crop=%i:%i:%i:%i[l]; [1:v]trim=start=%lldus,scale=%i:%i,fps=%f,setpts=PTS-STARTPTS,crop=%i:%i:%i:%i[r]; [l][r] hstack=inputs=2, trim=duration=%lldus\" -an %s", L.path, R.path, (long long)L.trim_time, (int)width, (int)height, framerate, w, h, x1, y1, (long long)R.trim_time, (int)width, (int)height, framerate, w, h, x2, y2, (long long)(duration - 40000), output);
			int r = system(ffmpeg_command);
			delete[] ffmpeg_command;
			return r;
		}
};

int trim_videos(VideoPair v, const char *output) {
	return v.trim(output);
}

int trim_videos(VideoPair v, float framerate, int64_t width, int64_t height, double cfx, double cfy, const char *output) {
	return v.trim(framerate, width, height, cfx, cfy, output);
}

int trim_videos(Video L, Video R, const char *output) {
	VideoPair vp(L, R);
	return vp.trim(output);
}

int trim_videos(Video L, Video R, float framerate, int64_t width, int64_t height, double cfx, double cfy, const char *output) {
	VideoPair vp(L, R);
	return vp.trim(framerate, width, height, cfx, cfy, output);
}

int trim_videos(const char* path_L, const char* path_R, const char *output) {
	VideoPair vp(path_L, path_R);
	return vp.trim(output);
}

int trim_videos(const char* path_L, const char* path_R, float framerate, int64_t width, int64_t height, double cfx, double cfy, const char *output) {
	VideoPair vp(path_L, path_R);
	return vp.trim(framerate, width, height, cfx, cfy, output);
}

int main(int argc, const char * argv[]) {

	const char *l_path = nullptr;
	const char *r_path = nullptr;
	const char *lf_path = nullptr;
	const char *rf_path = nullptr;
	const char *o_path = nullptr;
	const char *of_path = nullptr;

	// Parameters setup
	if (argc == 1) {
		cout << "Innovations LightX inc.\n\nusage: SynchFromEnd [options] [[infile options] -i infile]... {[outfile options] outfile}\n\n-auto\tAutomatically detect a video pair.\n-manual\tProcesses the videos assuming it will have the same filename.\n\n[infile options]\n-l,-r {PathToFile}\tFilename of the left and right videos (single pair).\n-lf,-rf {pathToFolder}\tFolder containing all left and right videos (expects the use of options -a or -m)\n\n[outfile options]\n-o {PathOfOutputFile}\tPath to assembled single pair of synchronized videos.\n-of {PathToOutputFolder}\tPath to assembled pairs of synchronized videos.\n\n";
		return 0;
	}
	else {
		for (int i = 1; i < argc; i++) {
			string arg = argv[i];
			if (arg == "-auto") {
				cout << "Automatic detection of video pairs not implemented yet!\n";
				return 0;
			} 
			else if (arg == "-manual") {
				// put manual bool = true;
			}
			else if (arg == "-l") {
				l_path = argv[++i];
			}
			else if (arg == "-lf") {
				lf_path = argv[++i];
			}
			else if (arg == "-r") {
				r_path = argv[++i];
			}
			else if (arg == "-rf") {
				rf_path = argv[++i];
			}
			else if (arg == "-o") {
				o_path = argv[++i];
			}
			else if (arg == "-of") {
				of_path = argv[++i];
			}
		}
		if (!((r_path != nullptr && l_path != nullptr) || (rf_path != nullptr && lf_path != nullptr))) {
			cout << "Error: SynchFromEnd need a pair of parameters -r,-l or -rf,-lf\n";
			return 0;
		}
		if (!(!(r_path == nullptr) != !(rf_path == nullptr))) {
			cout << "Error: Please specify a pair of video files or a pair of folders containing video files.\n";
			return 0;
		}
		if (!(o_path == nullptr || of_path == nullptr)) {
			cout << "Error: SynchFromEnd can only have one output file/folder\n";
			return 0;
		}
	}

	// tests path : G:\desktoop\lightX
	//l_path = "G://desktoop//lightx//testsss//iphoneG.MOV";
	l_path = "G://desktoop//lightx//GA.MOV";
	//r_path = "G://desktoop//lightx//testsss//iphoneD.MOV";
	r_path = "G://desktoop//lightx//DR.MOV";
	o_path = "\"G://OneDrive - Innovations LightX inc//OEIL_2K_30fps_FIXED.mp4\"";
//G:\OneDrive - Innovations LightX inc
	VideoPair VP(l_path, r_path);
	VP.trim(o_path, 2.2, 0.6, 30.0, 2);

	cout << "Done!\n";
	return 0;
}
