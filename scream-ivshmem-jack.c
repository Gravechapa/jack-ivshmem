// Scream-IVSHMEM receiver for JACK Audio Connection Kit.
// Based on Marco Martinelli's(https://github.com/martinellimarco) pulseaudio-ivshmem
// https://github.com/duncanthrax/scream/tree/master/Receivers/pulseaudio-ivshmem

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <stdalign.h>
#include <signal.h>
#include <math.h>

#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <jack/jack.h>
#include <jack/jslist.h>

#include <samplerate.h>

static jack_client_t *client;
static jack_port_t *output_ports[11];

static jack_nframes_t jack_sample_rate;
static jack_nframes_t jack_buffer_size;

static unsigned char current_channels = 2;
static uint16_t current_channel_map = 0x0003;

static pthread_mutex_t state_sync = PTHREAD_MUTEX_INITIALIZER;
static jack_default_audio_sample_t *audio_buffer = NULL;
static uint32_t audio_buffer_size = 0;
static uint32_t offset = 0;
static bool ready = false;

static bool stop = false;

struct shmheader
{
    uint32_t magic;
    uint16_t write_idx;
    uint8_t  offset;
    uint16_t max_chunks;
    uint32_t chunk_size;
    uint8_t  sample_rate;
    uint8_t  sample_size;
    uint8_t  channels;
    uint16_t channel_map;
};

static void show_usage(const char *arg0)
{
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: %s <ivshmem device path>\n", arg0);
    fprintf(stderr, "\n");
    exit(1);
}

static void * open_mmap(const char *shmfile)
{
    struct stat st;
    if (stat(shmfile, &st) < 0)
    {
        fprintf(stderr, "Failed to stat the shared memory file: %s\n", shmfile);
        exit(2);
    }

    int shmFD = open(shmfile, O_RDONLY);
    if (shmFD < 0)
    {
        fprintf(stderr, "Failed to open the shared memory file: %s\n", shmfile);
        exit(3);
    }

    void * map = mmap(0, st.st_size, PROT_READ, MAP_SHARED, shmFD, 0);
    if (map == MAP_FAILED)
    {
        fprintf(stderr, "Failed to map the shared memory file: %s\n", shmfile);
        close(shmFD);
        exit(4);
    }

    return map;
}

int process(jack_nframes_t nframes, void *arg)
{
    pthread_mutex_lock(&state_sync);
    if (ready)
    {
        JSList *output_buffers = NULL;
        // j is the key to map a windows SPEAKER_* position to a JACK ports
        // it goes from 0 (SPEAKER_FRONT_LEFT) up to 10 (SPEAKER_SIDE_RIGHT) following the order in ksmedia.h
        // the SPEAKER_TOP_* values are not used
        int j = 0;
        for (uint32_t i = 0; i < current_channels; ++i)
        {
            for (; j < 11;)
            {// check the channel map bit by bit from lsb to msb, starting from were we left on the previous step
                if ((current_channel_map >> j) & 0x01)// if the bit in j position is set then we have the key for this channel
                {
                    jack_default_audio_sample_t *out = jack_port_get_buffer(output_ports[j], nframes);
                    output_buffers = jack_slist_append(output_buffers, out);
                    ++j;
                    break;
                }
                ++j;
            }
        }
        for (uint32_t l = 0; l < nframes * current_channels; l += current_channels)
        {
            JSList *node = output_buffers;
            uint32_t k = 0;
            while (node != NULL)
            {
                ((jack_default_audio_sample_t*)node->data)[l / current_channels] = audio_buffer[k + l];

                node = jack_slist_next(node);
                ++k;
            }
        }
        //fwrite(audio_buffer, 4 * current_channels, nframes, stdout);
        //fwrite(output_buffers->data, 4, nframes, stdout);
        jack_slist_free(output_buffers);
        memmove(audio_buffer,
                &audio_buffer[nframes * current_channels],
                (audio_buffer_size - nframes) * current_channels * sizeof(jack_default_audio_sample_t));
        offset -= nframes;
        if (offset < jack_buffer_size)
        {
            ready = false;
        }
    }
    pthread_mutex_unlock(&state_sync);

    return 0;
}

void jack_shutdown(void *arg);

void jack_configure()
{
    jack_set_process_callback(client, process, NULL);
    jack_on_shutdown(client, jack_shutdown, NULL);

    jack_sample_rate = jack_get_sample_rate(client);
    jack_buffer_size = jack_get_buffer_size(client) * sizeof(float);

    printf("JACK client name: %s\n", jack_get_client_name(client));

    output_ports[0] = jack_port_register(client, "Front Left",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[1] = jack_port_register(client, "Front Right",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[2] = jack_port_register(client, "Front Center",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[3] = jack_port_register(client, "LFE / Subwoofer",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[4] = jack_port_register(client, "Rear Left",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[5] = jack_port_register(client, "Rear Right",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[6] = jack_port_register(client, "Front-Left Center",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[7] = jack_port_register(client, "Front-Right Center",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[8] = jack_port_register(client, "Rear Center",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[9] = jack_port_register(client, "Side Left",
                                         JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    output_ports[10] = jack_port_register(client, "Side Right",
                                          JACK_DEFAULT_AUDIO_TYPE,
                                          JackPortIsOutput, 0);

    for (int i = 0; i < 11; ++i)
    {
        if (!output_ports[i])
        {
            printf( "JACK cannot register port");
            exit(EXIT_FAILURE);
        }
    }

    if (jack_activate(client))
    {
        fprintf(stderr, "Cannot activate JACK client\n");
        exit(EXIT_FAILURE);
    }
}

void interrupt()
{
    stop = true;
}

void jack_shutdown(void *arg)
{
    interrupt();
}

int main(int argc, char*argv[])
{
    if (argc != 2)
    {
        show_usage(argv[0]);
    }

    signal(SIGINT, &interrupt);

    jack_status_t status;
    client = jack_client_open("scream-ivshmem", JackNullOption, &status);
    if (client == NULL)
    {
        fprintf(stderr, "jack_client_open() failed, "
                      "status = 0x%2.0x\n", status);
        if (status & JackServerFailed)
        {
          fprintf(stderr, "Unable to connect to JACK server\n");
        }
        exit(EXIT_FAILURE);
    }

    jack_configure();

    unsigned char * mmap = open_mmap(argv[1]);
    struct shmheader *header = (struct shmheader*)mmap;
    uint16_t read_idx = header->write_idx;

    unsigned char current_sample_rate = 0;
    unsigned char current_sample_size = 0;
    bool check = false;

    SRC_STATE *resampler = NULL;
    float *resample_buffer = NULL;
    double resample_ratio = 1.0;
    int resampler_type = SRC_SINC_BEST_QUALITY;
    bool enable_resampling = false;

    while (!stop)
    {
        if (header->magic != 0x11112014)
        {
            while (header->magic != 0x11112014)
            {
                usleep(10000);//10ms
            }
          read_idx = header->write_idx;
        }
        if (read_idx == header->write_idx)
        {
            usleep(10000);//10ms
            continue;
        }
        if (++read_idx == header->max_chunks)
        {
            read_idx = 0;
        }
        unsigned char *buffer = &mmap[header->offset + header->chunk_size * read_idx];
        uint32_t samples = header->chunk_size / ((header->sample_size / 8) * header->channels);

        if (current_sample_rate != header->sample_rate
         || current_sample_size != header->sample_size
         || current_channels != header->channels
         || current_channel_map != header->channel_map)
        {
            current_sample_rate = header->sample_rate;
            current_sample_size = header->sample_size;
            current_channels = header->channels;
            current_channel_map = header->channel_map;

            uint32_t rate = ((current_sample_rate >= 128) ? 44100 : 48000) * (current_sample_rate % 128);
            resample_ratio = (double)jack_sample_rate / (double)rate;

            if (current_sample_size != 32 && current_sample_size != 24 && current_sample_size != 16)
            {
                printf("Incompatible sample size %hhu,"
                       " not playing until next format switch.\n", current_sample_size);
                check = false;
                continue;
            }

            if (rate != jack_sample_rate)
            {
                int error;
                resampler = src_new(resampler_type, current_channels, &error);
                if (!resampler)
                {
                    fprintf(stderr, "%s", src_strerror(error));
                    exit(EXIT_FAILURE);
                }
                resample_buffer = malloc(samples * current_channels * sizeof(float));
                enable_resampling = true;
            }
            else
            {
                if (resample_buffer)
                {
                    free(resample_buffer);
                    resample_buffer = NULL;
                }
                if (resampler)
                {
                    src_delete(resampler);
                    resampler = NULL;
                }
                enable_resampling = false;
            }

            pthread_mutex_lock(&state_sync);
            offset = 0;
            ready = false;
            pthread_mutex_unlock(&state_sync);

            audio_buffer_size = jack_buffer_size > (uint32_t)ceil(samples * resample_ratio) ?
                        jack_buffer_size * 3 : (uint32_t)ceil(samples * resample_ratio) * 3;
            audio_buffer = realloc(audio_buffer, audio_buffer_size * current_channels * sizeof(jack_default_audio_sample_t));
            check = true;
        }

        if (check)
        {
            pthread_mutex_lock(&state_sync);
            if (audio_buffer_size - offset >= (uint32_t)ceil(samples * resample_ratio))
            {
                float *output_buffer;
                if (enable_resampling)
                {
                    output_buffer = resample_buffer;
                }
                else
                {
                    output_buffer = audio_buffer + offset * current_channels;
                }

                switch (current_sample_size)
                {
                    case 16:
                        src_short_to_float_array((int16_t*)buffer,
                                                  output_buffer,
                                                  samples * current_channels);
                        break;
                    case 24:
                    {
                        int size = samples * current_channels;
                        while (size)
                        {
                            --size;
                            int32_t tmp;
                            tmp = buffer[size * 3] | buffer[size * 3 + 1] << 8 | buffer[size * 3 + 2] << 16;
                            if ((tmp >> 16) & 0x80)
                            {
                                tmp |= 0xff << 24;
                            }
                            output_buffer[size] = (float)(tmp / (1.0 * 0x800000));
                        }
                        break;
                    }
                    case 32:
                        src_int_to_float_array((int32_t*)buffer,
                                                output_buffer,
                                                samples * current_channels);
                        break;
                    default:
                        continue;
                }

                if (enable_resampling)
                {
                    SRC_DATA data;
                    data.data_in = resample_buffer;
                    data.data_out = audio_buffer + offset * current_channels;
                    data.input_frames = samples;
                    data.output_frames = audio_buffer_size - offset;
                    data.end_of_input = 0;
                    data.src_ratio = resample_ratio;

                    int error;
                    error = src_process(resampler, &data);
                    if (error)
                    {
                        fprintf(stderr, "%s", src_strerror(error));
                        exit(EXIT_FAILURE);
                    }
                    if (samples != data.input_frames_used)
                    {
                        printf("Warning: not all frames been used during resampling");
                    }
                    offset += data.output_frames_gen;
                }
                else
                {
                    offset += samples;
                }

                if (offset >= jack_buffer_size)
                {
                    ready = true;
                }
            }
            pthread_mutex_unlock(&state_sync);
        }
    }

    if (client)
    {
        jack_client_close (client);
    }
    if (audio_buffer)
    {
        free(audio_buffer);
    }
    if (resample_buffer)
    {
        free(resample_buffer);
    }
    if (resampler)
    {
        src_delete(resampler);
    }
    pthread_mutex_destroy(&state_sync);
    return 0;
}
