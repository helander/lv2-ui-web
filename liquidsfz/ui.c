#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/patch/patch.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <pthread.h>

#include <netinet/in.h> // sockaddr_in
#include <sys/socket.h> // socket APIs
#include <unistd.h>     // open, close

#include <signal.h> // signal handling

#define SIZE 1024 // buffer size

#define BACKLOG 10 // number of pending connections queue will hold

#define UI_URI "http://helander.network/lv2uiweb/liquidsfz"


//#define SFZ_FILEPATH "/home/lehswe/compose/soundcan/SFZ/SalamanderGrandPianoV3_48khz24bit/SalamanderGrandPianoV3.sfz"

#define LIQUIDSFZ_URI      "http://spectmorph.org/plugins/liquidsfz"

/*
static char *definedControlKeys[] = {"upper.drawbar16",
                                     "upper.drawbar513",
                                     "upper.drawbar8",
                                     "upper.drawbar4",
                                     "upper.drawbar223",
                                     "upper.drawbar2",
                                     "upper.drawbar135",
                                     "upper.drawbar113",
                                     "upper.drawbar1",
                                     "lower.drawbar16",
                                     "lower.drawbar513",
                                     "lower.drawbar8",
                                     "lower.drawbar4",
                                     "lower.drawbar223",
                                     "lower.drawbar2",
                                     "lower.drawbar135",
                                     "lower.drawbar113",
                                     "lower.drawbar1",
                                     "pedal.drawbar16",
                                     "pedal.drawbar8",
                                     "percussion.enable",
                                     "percussion.decay",
                                     "percussion.harmonic",
                                     "percussion.volume",
                                     "vibrato.knob",
                                     "vibrato.routing",
                                     "vibrato.upper",
                                     "vibrato.lower",
                                     "swellpedal1",
                                     "rotary.speed-select",
                                     "overdrive.enable",
                                     "overdrive.character",
                                     "reverb.mix",
                                     "special.init",
                                     NULL};

static int nmbControlKeys = sizeof(definedControlKeys) / sizeof(char *);

typedef struct {
  char *key;
  uint8_t value;
  bool changed;
} PluginControl_t;
*/

typedef struct {
  LV2_Atom_Forge forge;
  LV2_URID_Map *map;
  LV2_URID_Unmap *unmap;
  LV2UI_Request_Value *request_value;
  LV2_Log_Logger logger;
  LV2_Options_Option *options;

  LV2UI_Write_Function write;
  LV2UI_Controller controller;

  char plugin_uri[100];
  char static_path[100];

  LV2_URID patch_Get;
  LV2_URID patch_Set;
  LV2_URID patch_property;
  LV2_URID patch_value;
  LV2_URID atom_eventTransfer;
  LV2_URID atom_Event;
  LV2_URID atom_Blank;
  LV2_URID atom_Object;
  LV2_URID atom_String;
  LV2_URID atom_Int;
  LV2_URID atom_Float;
  LV2_URID atom_URID;
  LV2_URID atom_Path;
  LV2_URID midi_MidiEvent;
  LV2_URID state_Changed;

//  LV2_URID bsynth_uiinit;
//  LV2_URID bsynth_controlmsg;
//  LV2_URID bsynth_midipgm;
//  LV2_URID bsynth_controlkey;
//  LV2_URID bsynth_controlval;

  LV2_URID liquidsfz_sfzfile;

  char *sfz_filepath;

  uint8_t forge_buf[1024];

//  PluginControl_t *pluginControls;
//  char program[128][100];
//  uint8_t currentProgram;
//  bool programChange;

  int http_port;
  pthread_t t_http_server;
  int serverSocket;
  int clientSocket;
  char *request;

} ThisUI;

/*
static PluginControl_t *getPluginControl(ThisUI *ui, char *key) {
  for (PluginControl_t *control = ui->pluginControls; control->key != NULL;
       control++) {
    if (!strcmp(key, control->key))
      return control;
  }
  return NULL;
}
*/

static void *http_server_run(void *inst);

static LV2UI_Handle instantiate(const LV2UI_Descriptor *descriptor,
                                const char *plugin_uri, const char *bundle_path,
                                LV2UI_Write_Function write_function,
                                LV2UI_Controller controller,
                                LV2UI_Widget *widget,
                                const LV2_Feature *const *features) {
  ThisUI *ui = (ThisUI *)calloc(1, sizeof(ThisUI));
  if (!ui) {
    return NULL;
  }
  ui->write = write_function;
  ui->controller = controller;
  sprintf(ui->plugin_uri, "%s", plugin_uri);
  sprintf(ui->static_path, "%sstatic", bundle_path);
  ui->http_port = atoi(getenv("HTTP_PORT"));
  ui->sfz_filepath = getenv("SFZ_FILEPATH");

  // Get host features
  // clang-format off
  const char* missing = lv2_features_query(
    features,
    LV2_LOG__log,         &ui->logger.log,    false,
    LV2_URID__map,        &ui->map,           true,
    LV2_URID__unmap,      &ui->unmap,           true,
    LV2_UI__requestValue, &ui->request_value, false,
    LV2_OPTIONS__options, &ui->options, false,
    NULL);
  // clang-format on

  lv2_log_logger_set_map(&ui->logger, ui->map);
  if (missing) {
    lv2_log_error(&ui->logger, "Missing feature <%s>\n", missing);
    free(ui);
    return NULL;
  }

  ui->patch_Get = ui->map->map(ui->map->handle, LV2_PATCH__Get);
  ui->patch_Set = ui->map->map(ui->map->handle, LV2_PATCH__Set);
  ui->patch_property = ui->map->map(ui->map->handle, LV2_PATCH__property);
  ui->patch_value = ui->map->map(ui->map->handle, LV2_PATCH__value);
  ui->atom_eventTransfer =
      ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
  ui->atom_Event = ui->map->map(ui->map->handle, LV2_ATOM__Event);
  ui->atom_Blank = ui->map->map(ui->map->handle, LV2_ATOM__Blank);
  ui->atom_Object = ui->map->map(ui->map->handle, LV2_ATOM__Object);
  ui->atom_String = ui->map->map(ui->map->handle, LV2_ATOM__String);
  ui->atom_Int = ui->map->map(ui->map->handle, LV2_ATOM__Int);
  ui->atom_Float = ui->map->map(ui->map->handle, LV2_ATOM__Float);
  ui->atom_URID = ui->map->map(ui->map->handle, LV2_ATOM__URID);
  ui->atom_Path = ui->map->map(ui->map->handle, LV2_ATOM__Path);
  ui->midi_MidiEvent = ui->map->map(ui->map->handle, LV2_MIDI__MidiEvent);
  ui->state_Changed = ui->map->map(
      ui->map->handle, "http://lv2plug.in/ns/ext/state#StateChanged");

/*
  ui->bsynth_uiinit =
      ui->map->map(ui->map->handle, "http://gareus.org/oss/lv2/b_synth#uiinit");
  ui->bsynth_controlmsg = ui->map->map(
      ui->map->handle, "http://gareus.org/oss/lv2/b_synth#controlmsg");
  ui->bsynth_midipgm = ui->map->map(
      ui->map->handle, "http://gareus.org/oss/lv2/b_synth#midipgm");
  ui->bsynth_controlkey = ui->map->map(
      ui->map->handle, "http://gareus.org/oss/lv2/b_synth#controlkey");
  ui->bsynth_controlval = ui->map->map(
      ui->map->handle, "http://gareus.org/oss/lv2/b_synth#controlval");
*/
 ui->liquidsfz_sfzfile  = ui->map->map (ui->map->handle, LIQUIDSFZ_URI "#sfzfile");

/*
  ui->pluginControls = calloc(nmbControlKeys, sizeof(PluginControl_t));
  for (int i = 0; i < nmbControlKeys; i++) {
    PluginControl_t *control = &ui->pluginControls[i];
    control->key = definedControlKeys[i];
    control->value = 0;
    control->changed = false;
  }
  for (int i = 0; i < 128; i++) {
    char *name = &ui->program[i][0];
    strcpy(name, "");
  }
*/

  lv2_atom_forge_init(&ui->forge, ui->map);

  int k = pthread_create(&ui->t_http_server, NULL, http_server_run, ui);
  if (k != 0) {
    fprintf(stderr, "%d : %s\n", k, "pthread_create : HTTPServer thread");fflush(stderr);
  }

  uint8_t obj_buf[400];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 400);

  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&ui->forge, 0);

  LV2_Atom *msg = (LV2_Atom *)lv2_atom_forge_object(&ui->forge, &frame, 0, ui->patch_Set);

  lv2_atom_forge_property_head (&ui->forge, ui->patch_property, 0);
  lv2_atom_forge_urid (&ui->forge, ui->liquidsfz_sfzfile);
  lv2_atom_forge_property_head (&ui->forge, ui->patch_value, 0);
  lv2_atom_forge_path (&ui->forge, ui->sfz_filepath, strlen(ui->sfz_filepath));
  lv2_atom_forge_pop(&ui->forge, &frame);

  ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->atom_eventTransfer,
            msg);

  return ui;
}


static void cleanup(LV2UI_Handle handle) {
  ThisUI *ui = (ThisUI *)handle;

  pthread_join(ui->t_http_server, NULL);
  close(ui->clientSocket);
  close(ui->serverSocket);
  if (ui->request != NULL)
    free(ui->request);

//  free(ui->pluginControls);
  free(ui);
}

/*
static void sendControls(ThisUI *ui) {
    char response[200];
    sprintf(response, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{");
    send(ui->clientSocket, response, strlen(response), 0);
    for (PluginControl_t *control = ui->pluginControls; control->key != NULL;
         control++) {
      if (control == ui->pluginControls)
        sprintf(response, "\"%s\": %d", control->key, control->value);
      else
        sprintf(response, ",\"%s\": %d", control->key, control->value);
      send(ui->clientSocket, response, strlen(response), 0);
    }
    send(ui->clientSocket, "}", 1, 0);
    close(ui->clientSocket);
}
*/

/*
static void an_object(ThisUI *ui, uint32_t port_index, LV2_Atom_Object *obj) {
  if (obj->body.otype == ui->bsynth_controlmsg) {
    fflush(stdout);
    LV2_Atom_String *keyAtom = NULL;
    LV2_Atom_Int *valueAtom = NULL;

    LV2_ATOM_OBJECT_FOREACH(obj, p) {
      if (p->key == ui->bsynth_controlkey)
        if (p->value.type == ui->atom_String)
          keyAtom = (LV2_Atom_String *)&p->value;
      if (p->key == ui->bsynth_controlval)
        if (p->value.type == ui->atom_Int)
          valueAtom = (LV2_Atom_Int *)&p->value;
    }
    if (keyAtom != NULL && valueAtom != NULL) {
      char *key = ((char *)keyAtom) + sizeof(LV2_Atom_String);
      uint8_t value = valueAtom->body;
      PluginControl_t *pluginControl = getPluginControl(ui, key);
      if (pluginControl != NULL) {
        pluginControl->value = value;
      } else {
        printf("\nNo Control defined for  key %s", key);
        fflush(stdout);
      }
    } else {
      printf("\nControl message property error");
      fflush(stdout);
    }
    return;
  }

  if (obj->body.otype == ui->bsynth_midipgm) {
    fflush(stdout);
    LV2_Atom_String *valueAtom = NULL;
    LV2_Atom_Int *keyAtom = NULL;

    LV2_ATOM_OBJECT_FOREACH(obj, p) {
      if (p->key == ui->bsynth_controlkey)
        if (p->value.type == ui->atom_Int)
          keyAtom = (LV2_Atom_Int *)&p->value;
      if (p->key == ui->bsynth_controlval)
        if (p->value.type == ui->atom_String)
          valueAtom = (LV2_Atom_String *)&p->value;
    }
    if (keyAtom != NULL && valueAtom != NULL) {
      char *value = ((char *)valueAtom) + sizeof(LV2_Atom_String);
      uint8_t key = keyAtom->body;
      strcpy(&ui->program[key][0], value);
    } else {
      printf("\nProgram message property error");
      fflush(stdout);
    }
    return;
  }

  if (obj->body.otype == ui->state_Changed) {
    sendControls(ui);
    return;
  }
}
*/

static void port_event(LV2UI_Handle handle, uint32_t port_index,
                       uint32_t buffer_size, uint32_t format,
                       const void *buffer) {
  ThisUI *ui = (ThisUI *)handle;
  if (!format)
    return;

  if (format != ui->atom_eventTransfer) {
    fprintf(stdout,
            "ThisUI: Unexpected (not event transfer) message format %d  %s.\n",
            format, ui->unmap->unmap(ui->unmap->handle, format));
    fflush(stdout);
    return;
  }

  LV2_Atom *atom = (LV2_Atom *)buffer;

  if (atom->type == ui->midi_MidiEvent) {
    return;
  }

  if (atom->type != ui->atom_Blank && atom->type != ui->atom_Object) {
    fprintf(stdout, "ThisUI: not an atom:Blank|Object msg. %d %s  \n",
            atom->type, ui->unmap->unmap(ui->unmap->handle, atom->type));
    return;
  }

//  LV2_Atom_Object *obj = (LV2_Atom_Object *)atom;

//  an_object(ui, port_index, obj);
}

/* Idle interface for UI. */
static int ui_idle(LV2UI_Handle handle) {
//  ThisUI *ui = (ThisUI *)handle;
/*
  for (PluginControl_t *control = ui->pluginControls; control->key != NULL;
       control++) {
    if (control->changed) {
      uint8_t obj_buf[2000];
      lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 2000);

      LV2_Atom_Forge_Frame frame;
      lv2_atom_forge_frame_time(&ui->forge, 0);

      LV2_Atom *msg = (LV2_Atom *)lv2_atom_forge_object(&ui->forge, &frame, 0,
                                                        ui->bsynth_controlmsg);
      lv2_atom_forge_property_head(&ui->forge, ui->bsynth_controlkey, 0);
      lv2_atom_forge_string(&ui->forge, control->key, strlen(control->key));
      lv2_atom_forge_property_head(&ui->forge, ui->bsynth_controlval, 0);
      lv2_atom_forge_int(&ui->forge, control->value);

      lv2_atom_forge_pop(&ui->forge, &frame);

      ui->write(ui->controller, 0, lv2_atom_total_size(msg),
                ui->atom_eventTransfer, msg);

      control->changed = false;
    }
  }

  if (ui->programChange) {
    uint8_t obj_buf[2000];
    lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 2000);

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&ui->forge, 0);

    LV2_Atom *msg = (LV2_Atom *)lv2_atom_forge_object(&ui->forge, &frame, 0,
                                                      ui->bsynth_midipgm);
    lv2_atom_forge_property_head(&ui->forge, ui->bsynth_controlkey, 0);
    lv2_atom_forge_int(&ui->forge, ui->currentProgram);

    lv2_atom_forge_pop(&ui->forge, &frame);

    ui->write(ui->controller, 0, lv2_atom_total_size(msg),
              ui->atom_eventTransfer, msg);

    ui->programChange = false;
  }
*/
  return 0;
}

static int noop() { return 0; }

static const void *extension_data(const char *uri) {
  static const LV2UI_Show_Interface show = {noop, noop};
  static const LV2UI_Idle_Interface idle = {ui_idle};

  if (!strcmp(uri, LV2_UI__showInterface)) {
    return &show;
  }

  if (!strcmp(uri, LV2_UI__idleInterface)) {
    return &idle;
  }

  return NULL;
}

static const LV2UI_Descriptor descriptor = {UI_URI, instantiate, cleanup,
                                            port_event, extension_data};

LV2_SYMBOL_EXPORT const LV2UI_Descriptor *lv2ui_descriptor(uint32_t index) {
  return index == 0 ? &descriptor : NULL;
}

static void handleSignal(int signal) {}

#define MTU_SIZE 1500
static void send_file_to_socket(char *filepath, int socket) {
  FILE *file = fopen(filepath, "rb");
  if (file == NULL) {
    char *stat404 = "HTTP/1.1 404 Not Found\r\n\r\n";
    send(socket, stat404, strlen(stat404), 0);
    return;
  }
  char *prefix = "HTTP/1.1 200 OK\r\n\r\n";
  send(socket, prefix, strlen(prefix), 0);
  unsigned char mtu[MTU_SIZE];
  int bytes_read;

  while ((bytes_read = fread(mtu, 1, MTU_SIZE, file)) > 0) {
    if (send(socket, mtu, bytes_read, 0) < bytes_read) {
      printf("\nError sending file data to client");
      fflush(stdout);
    }
  }
  fclose(file);
}

static void *http_server_run(void *inst) {
  ThisUI *ui = (ThisUI *)inst;

  signal(SIGINT, handleSignal);

  struct sockaddr_in serverAddress;
  serverAddress.sin_family = AF_INET; // IPv4
  serverAddress.sin_port =
      htons(ui->http_port); // port number in network byte order
                            // (host-to-network short)
  serverAddress.sin_addr.s_addr =
      htonl(INADDR_ANY); // localhost (host to network long)

  ui->serverSocket = socket(AF_INET, SOCK_STREAM, 0);

  setsockopt(ui->serverSocket, SOL_SOCKET, SO_REUSEADDR, &(int){1},
             sizeof(int));

  if (bind(ui->serverSocket, (struct sockaddr *)&serverAddress,
           sizeof(serverAddress)) < 0) {
    printf("Error: The server is not bound to the address.\n");
    return NULL;
  }

  if (listen(ui->serverSocket, BACKLOG) < 0) {
    printf("Error: The server is not listening.\n");
    return NULL;
  }

  while (1) {
    ui->request = (char *)malloc(SIZE * sizeof(char));
    char method[10], route[100];
//    char resource_string[50];
//    unsigned int resource_uint;

    ui->clientSocket = accept(ui->serverSocket, NULL, NULL);
    read(ui->clientSocket, ui->request, SIZE);

    sscanf(ui->request, "%s %s", method, route);

    free(ui->request);

    if (strcmp(method, "GET") != 0) {
      const char response[] =
          "HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
      send(ui->clientSocket, response, strlen(response), 0);
      continue;
    }

//    if (sscanf(route, "/control/%u/%s", &resource_uint, resource_string) == 2) {
      /*
      PluginControl_t *pluginControl = getPluginControl(ui, resource_string);
      if (pluginControl != NULL) {
        pluginControl->value = resource_uint;
        pluginControl->changed = true;
        char response[200];
        sprintf(response,
                "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n\r\n%d",
                pluginControl->value);
        send(ui->clientSocket, response, strlen(response), 0);
      } else {
        char *stat404 = "HTTP/1.1 404 Not Found\r\n\r\n";
        send(ui->clientSocket, stat404, strlen(stat404), 0);
      }
      */
//      close(ui->clientSocket);
//      continue;
//    }

/*
    if (!strcmp(route, "/controls")) {
      sendControls(ui);
      continue;
    }
*/
    float level;
    if (sscanf(route, "/level/%f", &level) == 1) {
        ui->write(ui->controller, 3, sizeof(level), 0, &level);
        char response[200];
        sprintf(response,
                "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n\r\n%f",
                level);
        send(ui->clientSocket, response, strlen(response), 0);
        close(ui->clientSocket);
        continue;
    }

/*
    if (!strcmp(route, "/programs")) {
      char response[200];
      sprintf(response,
              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nAccess-Control-Allow-Origin: *\r\n\r\n{");
      send(ui->clientSocket, response, strlen(response), 0);
      for (int i = 0; i < 128; i++) {
        if (i == 0)
           sprintf(response, "\"%d\": \"%s\"", i, &ui->program[i][0]);
        else {
           if (strlen(&ui->program[i][0]) > 0)
             sprintf(response, ",\"%d\": \"%s\"", i, &ui->program[i][0]);
           else
             strcpy(response,"");
        }
        send(ui->clientSocket, response, strlen(response), 0);
      }
      send(ui->clientSocket, "}", 1, 0);
      close(ui->clientSocket);
      continue;
    }
*/
    if (!strcmp(route, "/"))
      strcat(route, "index.html");

    char filepath[200];
    sprintf(filepath, "%s/%s", ui->static_path, &route[1]);
    send_file_to_socket(filepath, ui->clientSocket);
    close(ui->clientSocket);
  }
  return NULL;
}
