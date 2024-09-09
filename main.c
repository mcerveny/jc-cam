/*
SPDX-License-Identifier: MPL-2.0
SPDX-FileCopyrightText: 2023 Martin Cerveny <martin@c-home.cz>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libavformat/avformat.h>
#include <libavutil/dict.h>

#define AGENT "agent"

#if !defined(CAMAUTH) || !defined(CAMPORT)
#error CAMAUTH and CAMPORT must be defined
#endif

enum NALUnitType
{
    NAL_TRAIL_N = 0,
    NAL_TRAIL_R = 1,
    NAL_TSA_N = 2,
    NAL_TSA_R = 3,
    NAL_STSA_N = 4,
    NAL_STSA_R = 5,
    NAL_RADL_N = 6,
    NAL_RADL_R = 7,
    NAL_RASL_N = 8,
    NAL_RASL_R = 9,
    NAL_BLA_W_LP = 16,
    NAL_BLA_W_RADL = 17,
    NAL_BLA_N_LP = 18,
    NAL_IDR_W_RADL = 19,
    NAL_IDR_N_LP = 20,
    NAL_CRA_NUT = 21,
    NAL_VPS = 32,
    NAL_SPS = 33,
    NAL_PPS = 34,
    NAL_AUD = 35,
    NAL_EOS_NUT = 36,
    NAL_EOB_NUT = 37,
    NAL_FD_NUT = 38,
    NAL_SEI_PREFIX = 39,
    NAL_SEI_SUFFIX = 40
};

#define PRINTDBG(message, ...) fprintf(stdout, "%s:%d: " message, __FILE__, __LINE__, ##__VA_ARGS__)
#define AV_API_CALL(fn)                               \
    do                                                \
    {                                                 \
        int ret__ = fn;                               \
        if (ret__ < 0)                                \
        {                                             \
            PRINTDBG("AVERROR " #fn "=>%d\n", ret__); \
            exit(1);                                  \
        }                                             \
    } while (0)

uint8_t buf_packet[512 * 1024], *buf_packet_ptr;
AVFormatContext *muxer;

void write_packet(uint32_t frame_id, AVStream *video_track)
{
    AVPacket *encoded_packet = av_packet_alloc();

    if (buf_packet_ptr - buf_packet == 4)
        return;

    // PRINTDBG("WR %s %d %ld\n", frame_id, buf_packet_ptr - buf_packet);
    // write(f, buf_packet, buf_packet_ptr-buf_packet);
    AVRational encoder_time_base = (AVRational){1, 25};
    encoded_packet->pts = encoded_packet->dts = frame_id;
    encoded_packet->stream_index = video_track->index;
    int64_t scaled_pts = av_rescale_q(encoded_packet->pts, encoder_time_base, video_track->time_base);
    encoded_packet->pts = scaled_pts;
    int64_t scaled_dts = av_rescale_q(encoded_packet->dts, encoder_time_base, video_track->time_base);
    encoded_packet->dts = scaled_dts;
    encoded_packet->data = buf_packet;
    encoded_packet->size = buf_packet_ptr - buf_packet;
    AV_API_CALL(av_interleaved_write_frame(muxer, encoded_packet));
    av_packet_free(&encoded_packet);

    buf_packet_ptr = buf_packet + 4;
}

char buf[5 * 1024];
int rw(int s, int buflen)
{
    while (write(s, buf, buflen) == -1)
    {
        if (errno == EINTR)
            continue;
        assert(0);
    }
    while ((buflen = read(s, buf, sizeof(buf))) == -1)
    {
        if (errno == EINTR)
            continue;
        assert(0);
    }
    return buflen;
}

int main(int argc, char *argv[])
{
    struct protoent *protoent;
    struct sockaddr_in sockaddr_in;
    int s;
    int buflen;
    int cseq = 1;
    int session;
    char camuri[256];

    printf("VERSION %s\n", VERSION);

    if (argc != 5)
    {
        printf("usage: %s data_path camname matid posid\n", argv[0]);
        return 1;
    }

    PRINTDBG("BUILDTIME %s\n", __TIMESTAMP_ISO__);

    assert(argc == 5);
    char *path = argv[1];
    char *camname = argv[2];
    int matid = atoi(argv[3]), postition = atoi(argv[4]);

    snprintf(camuri, sizeof(camuri), "rtsp://%s:%d/profile0", camname, CAMPORT);
    PRINTDBG("%s\n", camuri);

    protoent = getprotobyname("tcp");
    assert(protoent);
    s = socket(AF_INET, SOCK_STREAM, protoent->p_proto);
    assert(s >= 0);
    struct hostent *cam = gethostbyname(camname);
    assert(cam);
    sockaddr_in.sin_addr.s_addr = ((struct in_addr *)(cam->h_addr_list[0]))->s_addr;
    sockaddr_in.sin_family = AF_INET;
    sockaddr_in.sin_port = htons(CAMPORT);
    assert(connect(s, (struct sockaddr *)&sockaddr_in, sizeof(sockaddr_in)) != -1);

    buflen = snprintf(buf, sizeof(buf), "OPTIONS %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: %s\r\n\r\n", camuri, cseq++, AGENT);
    rw(s, buflen);

    buflen = snprintf(buf, sizeof(buf), "DESCRIBE %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: %s\r\nAuthorization: Basic %s\r\nAccept: application/sdp\r\n\r\n", camuri, cseq++, AGENT, CAMAUTH);
    buflen = rw(s, buflen);
    buf[buflen] = '\0';
    sscanf(strstr(buf, "Session: "), "Session: %d", &session);

    buflen = snprintf(buf, sizeof(buf), "SETUP %s/track=v RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: %s\r\nAuthorization: Basic %s\r\nSession: %d\r\nTransport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n", camuri, cseq++, AGENT, CAMAUTH, session);
    rw(s, buflen);

    buflen = snprintf(buf, sizeof(buf), "SETUP %s/track=a RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: %s\r\nAuthorization: Basic %s\r\nSession: %d\r\nTransport: RTP/AVP/TCP;unicast;interleaved=2-3\r\n\r\n", camuri, cseq++, AGENT, CAMAUTH, session);
    rw(s, buflen);

    buflen = snprintf(buf, sizeof(buf), "PLAY %s RTSP/1.0\r\nCSeq: %d\r\nUser-Agent: %s\r\nAuthorization: Basic %s\r\nSession: %d\r\nRange: npt=0-\r\n\r\n", camuri, cseq++, AGENT, CAMAUTH, session);
    rw(s, buflen);

    int stat_sum = 0;
    buflen = 0;
    uint32_t stat_ts = 0;

    char fn[128], tfn[128];
    snprintf(fn, sizeof(fn) - 1, "%s/%s/", path, camname);
    mkdir(fn, 0755);
    uint64_t rts = 0;

    muxer = avformat_alloc_context();
    muxer->oformat = av_guess_format("mpegts", NULL, NULL);

    AVStream *video_track = avformat_new_stream(muxer, NULL);
    video_track->codecpar->codec_id = AV_CODEC_ID_HEVC;
    video_track->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    video_track->time_base = (AVRational){1, 25};
    video_track->avg_frame_rate = (AVRational){25, 1};

    AV_API_CALL(av_dict_set(&muxer->metadata, "service_provider", "judocare.cz", 0));
    char svcname[128];
    sprintf(svcname, "JUDOCARE-MAT%d.%d", matid, postition);
    AV_API_CALL(av_dict_set(&muxer->metadata, "service_name", svcname, 0));

    av_dump_format(muxer, 0, NULL, 1);

    uint32_t lastidr_frame_id = UINT32_MAX, actual_frame_id = UINT32_MAX;

    buf_packet_ptr = buf_packet;
    *(uint32_t *)buf_packet_ptr = htonl(1);
    buf_packet_ptr += 4;

    while (1)
    {
        int ret = read(s, buf + buflen, sizeof(buf) - buflen);
        if (ret <= 0)
        {
            PRINTDBG("ERR %s %d\n", camname, ret);
            assert(ret > 0);
        }
        buflen += ret;
        if (buflen < 16)
        {
            PRINTDBG("LOG %s buflen<16\n", camname);
            continue;
        }

        char *bufptr = buf;

        while (buflen >= 16)
        {
            assert(*bufptr == '$');
            uint8_t ch = *(uint8_t *)(bufptr + 1);
            uint16_t len = ntohs(*(uint16_t *)(bufptr + 2));
            if (buflen <= len + 4)
            {
                // PRINTDBG("--- buflen %d len %d\n", buflen, len);
                break;
            }

            // int mark = (*(uint8_t *)(bufptr + 5) & 0x80) == 0x80;
            // uint8_t type = *(uint8_t *)(bufptr + 5) & 0x7f;
            // uint16_t seq = ntohs(*(uint16_t *)(bufptr + 6));
            uint32_t ts = ntohl(*(uint32_t *)(bufptr + 8));
            // uint32_t ssi = ntohl(*(uint32_t *)(bufptr+12));

            uint32_t frame_id = ts / 3600;

            if (ch == 0)
            {

                if (ts >= stat_ts + (90000 * 20))
                {
                    // PRINTDBG("=================================================== %d\n", stat_sum/((ts-stat_ts)/90000));
                    stat_sum = 0;
                    stat_ts = ts;
                }

                // PRINTDBG("%d %3d:%d %10d %10d %4d %d\n",ch,type,mark,seq,ts,len-12,frame_id);
                uint8_t nal_unit_type = ((*(bufptr + 16) & 0x7e) >> 1);

                if (nal_unit_type < 48)
                {
                    // Single NAL Unit Packets
                    // PRINTDBG("nal %d\n", nal_unit_type);
                    // PRINTDBG("NALS %d\n",nal_unit_type);

                    if (nal_unit_type == NAL_VPS)
                    {
                        // start of refresh
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        rts = 1000ull * tv.tv_sec + tv.tv_usec / 1000;
                        PRINTDBG("CHUNK %s %lx\n", camname, rts);

                        if (actual_frame_id != UINT32_MAX && actual_frame_id + 1 != frame_id)
                            PRINTDBG("ERR %s %lx %u %u\n", camname, rts, actual_frame_id, frame_id);
                        write_packet(actual_frame_id, video_track);
                        actual_frame_id = frame_id;

                        if (lastidr_frame_id < UINT32_MAX)
                        {
                            AV_API_CALL(av_write_trailer(muxer));
                            AV_API_CALL(avio_close(muxer->pb));
                            rename(tfn, fn);

                            // if (frame_id < lastidr_frame_id)
                            //   video_track-> ->cur_dts = AV_NOPTS_VALUE;
                            //  hls_seq
                        }
                        lastidr_frame_id = frame_id;

                        snprintf(fn, sizeof(fn) - 1, "%s/%s/%lx.ts", path, camname, rts);
                        strcpy(tfn, fn);
                        strcat(tfn, "_");

                        AV_API_CALL(avio_open(&muxer->pb, tfn, AVIO_FLAG_WRITE));
                        AV_API_CALL(avformat_write_header(muxer, NULL));
                    }

                    if (actual_frame_id != frame_id)
                    {
                        if (actual_frame_id + 1 != frame_id)
                            PRINTDBG("ERR %s %lx %u %u\n", camname, rts, actual_frame_id, frame_id);
                        write_packet(actual_frame_id, video_track);
                    }

                    assert(buf_packet_ptr - buf_packet + len - 12 + 4 < sizeof(buf_packet));
                    bcopy(bufptr + 16, buf_packet_ptr, len - 12);
                    buf_packet_ptr += len - 12;
                    stat_sum += len - 12 + 2;

                    if (actual_frame_id == frame_id)
                    {
                        // PRINTDBG("continue\n");
                        *(uint32_t *)buf_packet_ptr = htonl(1);
                        buf_packet_ptr += 4;
                    }
                    actual_frame_id = frame_id;
                }
                else if (nal_unit_type == 49)
                {
                    // Fragmentation Units

                    if (*(bufptr + 18) & 0x80)
                    {

                        if (actual_frame_id != frame_id)
                        {
                            if (actual_frame_id + 1 != frame_id)
                                PRINTDBG("ERR %s %lx %u %u\n", camname, rts, actual_frame_id, frame_id);
                            write_packet(actual_frame_id, video_track);
                            actual_frame_id = frame_id;
                        }

                        // new_nal
                        *(uint16_t *)buf_packet_ptr = htons(((*(bufptr + 16) & 0x1) << 8) | ((*(bufptr + 18) & 0x3f) << 9) | (*(bufptr + 17)));
                        buf_packet_ptr += 2;

                        // uint8_t nal_unit_type = ((*(buf_packet_ptr - 2) & 0x7e) >> 1);
                        // PRINTDBG("NALC %d\n",nal_unit_type);
                        stat_sum += 2 + 4;
                    }

                    assert(actual_frame_id == frame_id);
                    assert(buf_packet_ptr - buf_packet + len - 12 - 3 < sizeof(buf_packet));
                    bcopy(bufptr + 16 + 3, buf_packet_ptr, len - 12 - 3);
                    buf_packet_ptr += len - 12 - 3;
                    // PRINTDBG("ADD: %d off %ld\n", len-12-3, buf_packet_ptr-buf_packet);
                    stat_sum += len - 12 - 3;
                }
                else
                    assert(0);
            }

            bufptr += len + 4;
            buflen -= len + 4;
        }
        memmove(buf, bufptr, buflen);
    }

    return 0;
}
