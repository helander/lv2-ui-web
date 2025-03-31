#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/core/lv2_util.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/midi/midi.h>
#include <lv2/patch/patch.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <lv2/options/options.h>
#include <lilv/lilv.h>

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <string.h>

#include <pthread.h>

#include <sys/socket.h> // socket APIs
#include <netinet/in.h> // sockaddr_in
#include <unistd.h>     // open, close

#include <signal.h> // signal handling

#define SIZE 1024  // buffer size

#define BACKLOG 10 // number of pending connections queue will hold


#define UI_URI "http://helander.network/lv2uiweb/bsynth"


static char  *definedControlKeys[] =  {
"upper.drawbar16",
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
NULL
};

static int nmbControlKeys = sizeof(definedControlKeys)/sizeof(char *);

typedef struct {
  char *key;
  uint8_t value;
  bool changed;
} PluginControl_t;

typedef struct
{
    LV2_Atom_Forge forge;
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;
    LV2UI_Request_Value* request_value;
    LV2_Log_Logger logger;
    LV2_Options_Option* options;

    LV2UI_Write_Function write;
    LV2UI_Controller controller;

    char plugin_uri[100];
    char static_path[100];

    int idleLoopCounter;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
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

    LV2_URID bsynth_uiinit;
    LV2_URID bsynth_controlmsg;
    LV2_URID bsynth_controlkey;
    LV2_URID bsynth_controlval;

    uint8_t forge_buf[1024];

    PluginControl_t  *pluginControls;

     int serverSocket;
     int clientSocket;
     char *request;
     pthread_t t_http_server;
     int http_port;

} ThisUI;

/*
static void dumpmem(void* start, int bytes_per_row, int rows)
{
    char* g = (char*)start;
    int n = 0;
    for (int row = 0; row < rows; row++) {
        printf("\n %04x  ", n);
        for (int k = 0; k < bytes_per_row; k++)
            printf(" %02x", g[n + k]);
        n += bytes_per_row;
    }
    fflush(stdout);
}
*/



static PluginControl_t *getPluginControl(ThisUI *ui, char *key) {
   for(PluginControl_t *control = ui->pluginControls; control->key != NULL; control++) {
      if (!strcmp(key,control->key)) return control;
   }
   return NULL; 
}


static void *http_server_run(void* inst);


static LV2UI_Handle instantiate(const LV2UI_Descriptor* descriptor, const char* plugin_uri, const char* bundle_path,
    LV2UI_Write_Function write_function, LV2UI_Controller controller, LV2UI_Widget* widget,
    const LV2_Feature* const* features)
{
    ThisUI* ui = (ThisUI*)calloc(1, sizeof(ThisUI));
    if (!ui) {
        return NULL;
    }
    ui->write = write_function;
    ui->controller = controller;
    sprintf(ui->plugin_uri,"%s",plugin_uri);
    sprintf(ui->static_path,"%sstatic",bundle_path);
    ui->idleLoopCounter = 0;
    ui->http_port = 25550;


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
    ui->atom_eventTransfer = ui->map->map(ui->map->handle, LV2_ATOM__eventTransfer);
    ui->atom_Event = ui->map->map(ui->map->handle, LV2_ATOM__Event);
    ui->atom_Blank = ui->map->map(ui->map->handle, LV2_ATOM__Blank);
    ui->atom_Object = ui->map->map(ui->map->handle, LV2_ATOM__Object);
    ui->atom_String = ui->map->map(ui->map->handle, LV2_ATOM__String);
    ui->atom_Int = ui->map->map(ui->map->handle, LV2_ATOM__Int);
    ui->atom_Float = ui->map->map(ui->map->handle, LV2_ATOM__Float);
    ui->atom_URID = ui->map->map(ui->map->handle, LV2_ATOM__URID);
    ui->atom_Path = ui->map->map(ui->map->handle, LV2_ATOM__Path);
    ui->midi_MidiEvent = ui->map->map(ui->map->handle, LV2_MIDI__MidiEvent);

    ui->bsynth_uiinit = ui->map->map(ui->map->handle, "http://gareus.org/oss/lv2/b_synth#uiinit");
    ui->bsynth_controlmsg = ui->map->map(ui->map->handle, "http://gareus.org/oss/lv2/b_synth#controlmsg");
    ui->bsynth_controlkey = ui->map->map(ui->map->handle, "http://gareus.org/oss/lv2/b_synth#controlkey");
    ui->bsynth_controlval = ui->map->map(ui->map->handle, "http://gareus.org/oss/lv2/b_synth#controlval");



    ui->pluginControls = calloc(nmbControlKeys,sizeof(PluginControl_t));
    for (int i=0; i < nmbControlKeys; i++) {
        PluginControl_t *control = &ui->pluginControls[i];
        control->key = definedControlKeys[i];
        control->value = 0;
        control->changed = false;
    }


    lv2_atom_forge_init(&ui->forge, ui->map);



        int k = pthread_create (&ui->t_http_server, NULL, http_server_run, ui);
        if (k != 0) {
                fprintf (stderr, "%d : %s\n", k, "pthread_create : HTTPServer thread");
        }


    uint8_t obj_buf[400];
    lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 400);

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time (&ui->forge, 0);

    LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object (&ui->forge, &frame, 0, ui->bsynth_uiinit);

    lv2_atom_forge_pop (&ui->forge, &frame);

    ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->atom_eventTransfer, msg);


    return ui;
}

static void cleanup(LV2UI_Handle handle)
{
    ThisUI* ui = (ThisUI*)handle;

    pthread_join (ui->t_http_server, NULL);
    close(ui->clientSocket);
    close(ui->serverSocket);
    if (ui->request != NULL)
      free(ui->request);

    free(ui->pluginControls);
    free(ui);
}

static void
an_object(ThisUI* ui,uint32_t port_index, LV2_Atom_Object* obj)
{
   if (obj->body.otype == ui->bsynth_controlmsg) {
      //printf("\nObject type bsynth-controlmsg");fflush(stdout);
      LV2_Atom_String* keyAtom = NULL;
      LV2_Atom_Int* valueAtom = NULL;

      LV2_ATOM_OBJECT_FOREACH(obj, p)
      {
        if (p->key == ui->bsynth_controlkey) if (p->value.type == ui->atom_String) keyAtom = (LV2_Atom_String*)&p->value;
        if (p->key == ui->bsynth_controlval) if (p->value.type == ui->atom_Int) valueAtom = (LV2_Atom_Int*)&p->value;
      }
      if (keyAtom != NULL && valueAtom != NULL) {
        char *key = ((char*)keyAtom) + sizeof(LV2_Atom_String);
        uint8_t value = valueAtom->body; 
         //printf("\nControl message key %s  value %d",key,value);fflush(stdout);
         PluginControl_t *pluginControl = getPluginControl(ui,key);
         if (pluginControl != NULL) {
            pluginControl->value = value;
         } else {
            printf("\nNo Control defined for  key %s",key);fflush(stdout);
         }
      } else {
         printf("\nControl message property error");fflush(stdout);
      }

   }

/*
    char message[1000];
    sprintf(message, "|port|%d|object|%s|", port_index, ui->unmap->unmap(ui->unmap->handle, obj->body.otype));
    LV2_ATOM_OBJECT_FOREACH(obj, p)
    {
        if (p->value.type == ui->atom_Int) {
            LV2_Atom_Int* intAtom = (LV2_Atom_Int*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|integer|value|%d|", ui->unmap->unmap(ui->unmap->handle, p->key), intAtom->body);
        } else if (p->value.type == ui->atom_Float) {
            LV2_Atom_Float* floatAtom = (LV2_Atom_Float*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|float|value|%f|", ui->unmap->unmap(ui->unmap->handle, p->key), floatAtom->body);
        } else if (p->value.type == ui->atom_String) {
            LV2_Atom_String* stringAtom = (LV2_Atom_String*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|string|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ((char*)stringAtom) + sizeof(LV2_Atom_String));
        } else if (p->value.type == ui->atom_Path) {
            LV2_Atom_String* pathAtom = (LV2_Atom_String*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|path|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ((char*)pathAtom) + sizeof(LV2_Atom_String));
        } else if (p->value.type == ui->atom_URID) {
            LV2_Atom_URID* uridAtom = (LV2_Atom_URID*)&p->value;
            sprintf(message + strlen(message), "key|%s|type|uri|value|%s|", ui->unmap->unmap(ui->unmap->handle, p->key), ui->unmap->unmap(ui->unmap->handle,uridAtom->body));
        } else if (p->value.type == ui->atom_Object) {
            an_object(ui, port_index, (LV2_Atom_Object*)p);
        } else {
            printf("\n Unsupported atom type %s  size %d ", ui->unmap->unmap(ui->unmap->handle, p->value.type),p->value.size);
            fflush(stdout);
            return;
        }
    }
    printf("\nMESSAGE %s", message);fflush(stdout); 
*/
}

static void port_event(LV2UI_Handle handle, uint32_t port_index, uint32_t buffer_size, uint32_t format,
    const void* buffer)
{
    ThisUI* ui = (ThisUI*)handle;
    if(!format) return;

    if (format != ui->atom_eventTransfer) {
        fprintf(stdout, "ThisUI: Unexpected (not event transfer) message format %d  %s.\n",format,ui->unmap->unmap(ui->unmap->handle,format));
        fflush(stdout);
        return;
    }

    LV2_Atom* atom = (LV2_Atom*)buffer;
    //fprintf(stdout, "ThisUI: -Atom size %d  type  %d %s  \n",atom->size,atom->type,ui->unmap->unmap(ui->unmap->handle,atom->type));

    if (atom->type == ui->midi_MidiEvent) {
        return;
    }

    if (atom->type != ui->atom_Blank && atom->type != ui->atom_Object) {
        fprintf(stdout, "ThisUI: not an atom:Blank|Object msg. %d %s  \n",atom->type,ui->unmap->unmap(ui->unmap->handle,atom->type));
        return;
    }

    LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;

    an_object(ui, port_index, obj);
}

/* Idle interface for UI. */
static int ui_idle(LV2UI_Handle handle)
{
    ThisUI* ui = (ThisUI*)handle;
    ui->idleLoopCounter++;
    if (ui->idleLoopCounter > 50) {
      //printf("\nLoop count");fflush(stdout);
      ui->idleLoopCounter = 0;
    }

   for(PluginControl_t *control = ui->pluginControls; control->key != NULL; control++) {
      if (control->changed) {
         printf("\nControl %s changed",control->key);fflush(stdout);


    uint8_t obj_buf[2000];
    lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 2000);

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time (&ui->forge, 0);

    LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_object (&ui->forge, &frame, 0, ui->bsynth_controlmsg);
    lv2_atom_forge_property_head (&ui->forge, ui->bsynth_controlkey, 0);
    lv2_atom_forge_string (&ui->forge, control->key, strlen (control->key));
    lv2_atom_forge_property_head (&ui->forge, ui->bsynth_controlval, 0);
    lv2_atom_forge_int (&ui->forge, control->value);

    lv2_atom_forge_pop (&ui->forge, &frame);

    ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->atom_eventTransfer, msg);

         control->changed = false;

      }
   }



/*
    char message[MSG_BUFFER_SIZE];
    int bytes = 0;
    bytes = mq_receive(ui->mq_input, message, MSG_BUFFER_SIZE, NULL);
    if (bytes == -1)
        return 0; // No messages availble
    message[bytes] = 0;
//    printf("\nMessage with %d bytes received  %s", bytes, message);fflush(stdout);

    uint8_t obj_buf[2000];
    lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 2000);
    LV2_Atom* msg = NULL;

    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time (&ui->forge, 0);

    char *token = strtok(message, "|");

    if (strcmp(token,"port")) return 0;
    token = strtok(NULL,"|");
    uint32_t portIndex = atoi(token);
    token = strtok(NULL,"|");
    if (!strcmp(token,"control")) {
       token = strtok(NULL,"|");
       float value = atof(token);
       ui->write(ui->controller, portIndex, sizeof(float),  0, &value);
       return 0;
    }
    if (!strcmp(token,"object")) {
       token = strtok(NULL,"|");
       if (token) {
          LV2_URID object = ui->map->map(ui->map->handle, token);
          msg = (LV2_Atom*)lv2_atom_forge_object (&ui->forge, &frame, 0, object);
          token = strtok(NULL,"|");
          while (token != NULL) {
            if (!strcmp(token,"key")) {
	      token = strtok(NULL,"|");
              if (token) {
                 LV2_URID key = ui->map->map(ui->map->handle, token);
                 token = strtok(NULL,"|");
                 if (token) {
                   if (!strcmp(token,"type")) {
                      token = strtok(NULL,"|");
                      if (token) {
                        char *type = token;
                        token = strtok(NULL,"|");
                        if (token) {
                           if (!strcmp(token,"value")) {
                              token = strtok(NULL,"|");
                              if (token) {
                                 char *value = token;
                                 //printf("\nobject %d key %d type %s value %s",object,key,type,value);fflush(stdout);
                                 lv2_atom_forge_property_head (&ui->forge, key, 0);
                                 if (!strcmp(type,"integer")) {
                                    lv2_atom_forge_int (&ui->forge, atoi(value));
                                 } else if (!strcmp(type,"string")){
                                    lv2_atom_forge_string (&ui->forge, value, strlen (value));
                                 } else if (!strcmp(type,"path")){
                                    lv2_atom_forge_path (&ui->forge, value, strlen (value));
                                 } else if (!strcmp(type,"uri")){
                                    lv2_atom_forge_urid (&ui->forge, ui->map->map(ui->map->handle, value));
                                 }
                              }
                           }
                        }
                      }
                   }
                 }
              }
            }
            token = strtok(NULL, "|");
         }
       }
    }

    lv2_atom_forge_pop (&ui->forge, &frame);

    if (msg)
        ui->write(ui->controller, portIndex, lv2_atom_total_size(msg), ui->atom_eventTransfer, msg);
*/
    return 0;
}

static int noop()
{
    return 0;
}

static const void* extension_data(const char* uri)
{
    static const LV2UI_Show_Interface show = { noop, noop };
    static const LV2UI_Idle_Interface idle = { ui_idle };

    if (!strcmp(uri, LV2_UI__showInterface)) {
        return &show;
    }

    if (!strcmp(uri, LV2_UI__idleInterface)) {
        return &idle;
    }

    return NULL;
}

static const LV2UI_Descriptor descriptor = { UI_URI, instantiate, cleanup, port_event, extension_data };

LV2_SYMBOL_EXPORT const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}




//static void NoCommandResponse(ThisUI *ui) {
//      const char response[] = "HTTP/1.1 404 No such command\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
//      send(ui->clientSocket, response, strlen(response), 0);
//}


static void handleSignal(int signal)
{
}


#define MTU_SIZE 1500
static void send_file_to_socket(char *filepath, int socket) {
	printf("\nSending file %s",filepath);fflush(stdout);
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

	while (( bytes_read = fread(mtu, 1, MTU_SIZE, file)) > 0 ) {
        	if ( send(socket, mtu, bytes_read, 0) < bytes_read ) {
        		printf("\nError sending file data to client");fflush(stdout);
		}
	}
        fclose(file);
}

static void *http_server_run(void* inst)
{
  ThisUI *ui = (ThisUI *) inst;

  signal(SIGINT, handleSignal);

  struct sockaddr_in serverAddress;
  serverAddress.sin_family = AF_INET;                     // IPv4
  serverAddress.sin_port = htons(ui->http_port);                   // port number in network byte order (host-to-network short)
  serverAddress.sin_addr.s_addr = htonl(INADDR_ANY); // localhost (host to network long)

  ui->serverSocket = socket(AF_INET, SOCK_STREAM, 0);

  setsockopt(ui->serverSocket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

  if (bind(ui->serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0)
  {
    printf("Error: The server is not bound to the address.\n");
    return NULL;
  }

  if (listen(ui->serverSocket, BACKLOG) < 0)
  {
    printf("Error: The server is not listening.\n");
    return NULL;
  }


  while (1)
  {
    ui->request = (char *)malloc(SIZE * sizeof(char));
    char method[10], route[100];

    ui->clientSocket = accept(ui->serverSocket, NULL, NULL);
    read(ui->clientSocket, ui->request, SIZE);

    sscanf(ui->request, "%s %s", method, route);

    free(ui->request);
    printf("\nMethod %s  Path %s\n",method,route);
    fflush(stdout);


    if (strcmp(method, "GET") != 0)
    {
      const char response[] = "HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
      send(ui->clientSocket, response, strlen(response), 0);
    }
    else
    {

      char *token;


      token = strtok(route, "/");
      if (token == NULL) {
         char filepath[200];
	 sprintf(filepath,"%s/%s",ui->static_path,"index.html");
         send_file_to_socket(filepath,ui->clientSocket);
      } else if (strcmp(token,"control") == 0) {
           char *sKey =NULL;
           token = strtok(NULL, "/");
           if (token != NULL) { sKey = token;  token = strtok(NULL, "/");}
           if (sKey != NULL) {
              char *sValue = NULL;
              //token = strtok(NULL, "/");
              if (token != NULL) { sValue = token;  token = strtok(NULL, "/");}
              printf("\nParsed  control key %s  value %s",sKey,sValue);fflush(stdout);
              PluginControl_t *pluginControl = getPluginControl(ui,sKey);
              if (pluginControl != NULL) {
                 if (sValue != NULL) {
                   pluginControl->value = atoi(sValue);
                   pluginControl->changed = true;
                 }
                 char response[200];
                 sprintf(response,"HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n\r\n%d",pluginControl->value);
                 send(ui->clientSocket, response, strlen(response), 0);
              } else {
	        char *stat404 = "HTTP/1.1 404 Not Found\r\n\r\n";
        	send(ui->clientSocket, stat404, strlen(stat404), 0);
              }
           } else {
	        char *stat404 = "HTTP/1.1 404 Not Found\r\n\r\n";
        	send(ui->clientSocket, stat404, strlen(stat404), 0);
           }
      } else {
         char filepath[200];
	 sprintf(filepath,"%s/%s",ui->static_path,&route[1]);
         send_file_to_socket(filepath,ui->clientSocket);
      } 

    }
    close(ui->clientSocket);
    printf("\n");
  }
  return NULL;
}

