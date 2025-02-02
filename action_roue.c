#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#define CONFIG_FILE "./params.cfg"  // Le fichier de configuration est dans le même répertoire que l'exécutable
#define SYS_HIDRAW_PATH "/sys/class/hidraw"
#define BUFFER_SIZE 8

// Identifiants du périphérique recherché
#define DEVICE_HID_ID "0018:00000B05:00000220"
#define DEVICE_HID_NAME "ASUS2020:00 0B05:0220"
#define DEVICE_MODALIAS "hid:b0018g0001v00000B05p00000220"

typedef struct {
    char action[50];
    KeySym key1;
    KeySym key2;
} KeyBinding;

KeyBinding bindings[10];
int binding_count = 0;

int is_target_device(const char *hidraw) {
    char uevent_path[512];
    snprintf(uevent_path, sizeof(uevent_path), "%s/%s/device/uevent", SYS_HIDRAW_PATH, hidraw);

    FILE *file = fopen(uevent_path, "r");
    if (!file) return 0;

    char line[256];
    int match = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, DEVICE_HID_ID) || strstr(line, DEVICE_HID_NAME) || strstr(line, DEVICE_MODALIAS)) {
            match++;
        }
    }
    fclose(file);
    return (match >= 1);
}

char *find_hidraw_device() {
    static char device_path[64] = "";
    struct dirent *entry;
    DIR *dir = opendir(SYS_HIDRAW_PATH);
    if (!dir) {
        perror("Erreur ouverture hidraw");
        return NULL;
    }
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        if (is_target_device(entry->d_name)) {
            snprintf(device_path, sizeof(device_path), "/dev/%s", entry->d_name);
            closedir(dir);
            return device_path;
        }
    }
    closedir(dir);
    return NULL;
}

KeySym get_keysym_from_string(const char *key) {
    if (strcasecmp(key, "CTRL") == 0) return XK_Control_L;
    if (strcasecmp(key, "ALT") == 0) return XK_Alt_L;
    if (strcasecmp(key, "SHIFT") == 0) return XK_Shift_L;
    if (strcasecmp(key, "SUPER") == 0) return XK_Super_L;
    if (strcasecmp(key, "PAGEUP") == 0) return XK_Page_Up;
    if (strcasecmp(key, "PAGEDOWN") == 0) return XK_Page_Down;
    if (strcasecmp(key, "UP") == 0) return XK_Up;
    if (strcasecmp(key, "DOWN") == 0) return XK_Down;
    return XStringToKeysym(key);
}

void load_config() {
    FILE *file = fopen(CONFIG_FILE, "r");
    if (!file) {
        perror("Impossible d'ouvrir params.cfg");
        return;
    }
   char line[128];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || strlen(line) < 3) continue;
        char action[50], key1[20], key2[20] = "";
        if (sscanf(line, "%49[^=]=%19[^+]+%19s", action, key1, key2) >= 2) {
            bindings[binding_count].key1 = get_keysym_from_string(key1);
            bindings[binding_count].key2 = (strlen(key2) > 0) ? get_keysym_from_string(key2) : 0;
            strcpy(bindings[binding_count].action, action);
            binding_count++;
            printf("Chargé: %s -> %s + %s\n", action, key1, key2); // Debug: affiche les actions
        }
    }
    fclose(file);
}

void send_key_combination(KeySym key1, KeySym key2) {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Erreur d'ouverture de la connexion X\n");
        return;
    }
    if (key1) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key1), True, 0);
    if (key2) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key2), True, 0);
    if (key2) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key2), False, 0);
    if (key1) XTestFakeKeyEvent(display, XKeysymToKeycode(display, key1), False, 0);
    XFlush(display);
    XCloseDisplay(display);
}

void execute_action(const char *action) {
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].action, action) == 0) {
            send_key_combination(bindings[i].key1, bindings[i].key2);
            return;
        }
    }
    // Gestion des actions non reconnues sans arrêter le programme
    printf("Action inconnue : %s\n", action);
}

void read_hidraw_data(const char *device_path) {
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Erreur ouverture HID");
        return;
    }
    uint8_t data[BUFFER_SIZE];
    uint8_t last_data[BUFFER_SIZE] = {0}; // Pour stocker les données précédentes

    while (1) {
        ssize_t bytesRead = read(fd, data, BUFFER_SIZE);
        if (bytesRead < 0) {
            perror("Erreur de lecture du périphérique HID");
            break;  // On sort de la boucle si une erreur se produit
        }

        if (bytesRead > 0) {
            if (memcmp(data, (uint8_t[]){0x01, 0x00, 0x01, 0x00}, 4) == 0) {
                execute_action("ACTION_TOURNER_DROITE");
                printf("ACTION_ROUE_DROITE\n");
            }
            else if (memcmp(data, (uint8_t[]){0x01, 0x00, 0xff, 0xff}, 4) == 0) {
                execute_action("ACTION_TOURNER_GAUCHE");
                printf("ACTION_ROUE_GAUCHE\n");
            }

            // Détection des clics
            else if (memcmp(data, (uint8_t[]){0x01, 0x01, 0x00, 0x00}, 4) == 0) {
                // Si on détecte {0x01, 0x01, 0x00, 0x00}, on attend pour voir ce qui suit
                printf("Clic... En attente de l'action suivante...\n");
                // On mémorise cet état
                memcpy(last_data, data, sizeof(last_data));
            }

            else if (memcmp(data, (uint8_t[]){0x01, 0x00, 0x00, 0x00}, 4) == 0) {
                // Si on détecte {0x01, 0x00, 0x00, 0x00} après {0x01, 0x01, 0x00, 0x00}, on prend en compte le relâchement
                if (memcmp(last_data, (uint8_t[]){0x01, 0x01, 0x00, 0x00}, 4) == 0) {
                    memset(last_data, 0, sizeof(last_data));
                    printf("Relâchement après clic, prendre en compte.\n");
                    execute_action("ACTION_CLIC_ROUE");
                }
            }

            else if (memcmp(data, (uint8_t[]){0x01, 0x01, 0x01, 0x00}, 4) == 0) {
                if (memcmp(last_data, (uint8_t[]){0x01, 0x01, 0x00, 0x00}, 4) == 0 ||
                    memcmp(last_data, (uint8_t[]){0x01, 0x01, 0x01, 0x00}, 4) == 0 ||
                    memcmp(last_data, (uint8_t[]){0x01, 0x01, 0xff, 0xff}, 4) == 0) {
                    memcpy(last_data, data, sizeof(last_data));
                    printf("Clic + tourne a droite.\n");
                    execute_action("ACTION_APPUYER_ROUE_DR");
                }
            }
            else if (memcmp(data, (uint8_t[]){0x01, 0x01, 0xff, 0xff}, 4) == 0) {
                // Si on détecte {0x01, 0x01, 0xff, 0xff} après {0x01, 0x01, 0x00, 0x00}, on ignore le précédent
                if (memcmp(last_data, (uint8_t[]){0x01, 0x01, 0x00, 0x00}, 4) == 0 ||
                    memcmp(last_data, (uint8_t[]){0x01, 0x01, 0x01, 0x00}, 4) == 0 ||
                    memcmp(last_data, (uint8_t[]){0x01, 0x01, 0xff, 0xff}, 4) == 0) {
                    memcpy(last_data, data, sizeof(last_data));
                    printf("Clic + tourne a gauche.\n");
                    execute_action("ACTION_APPUYER_ROUE_GA");
                }
            }

            else {
                // Si aucune action n'est reconnue, on l'ignore et continue
                printf("Action inconnue, aucune action effectuée.\n");
            }
        }
    }
    close(fd);
}

int main() {
    load_config();
    char *device_path = find_hidraw_device();
    if (!device_path) {
        printf("Aucun périphérique HID trouvé.\n");
        return EXIT_FAILURE;
    }
    read_hidraw_data(device_path);
    return EXIT_SUCCESS;
}
