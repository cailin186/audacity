#ifndef MPEG3_VTRACK_H
#define MPEG3_VTRACK_H

#include "mpeg3demux.h"
#include "video/mpeg3video.h"

typedef struct
{
	int width;
	int height;
	float frame_rate;
	float aspect_ratio;
	mpeg3_demuxer_t *demuxer;
	mpeg3video_t *video;
	long current_position;  /* Number of next frame to be played */
	long total_frames;     /* Total frames in the file */
} mpeg3_vtrack_t;

#endif
