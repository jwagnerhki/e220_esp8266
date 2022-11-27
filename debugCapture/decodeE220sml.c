#include <stdio.h>
#include <math.h>
#include <sys/types.h>

#include <sml/sml_file.h>
#include <sml/sml_transport.h>
#include <sml/sml_value.h>

#include "unit.h" // provides dlms_get_unit(), copied from https://raw.githubusercontent.com/volkszaehler/vzlogger/master/include/unit.h

#define SML_RAW_LEN 226				// 196 is next multiple-of-4, pad 2 zeroes
const char *SML_RAW_FILE = "sml.bin";

void transport_receiver(unsigned char *buffer, size_t buffer_len);

int main(void)
{
	unsigned char buf[SML_RAW_LEN+2+4];

	FILE* fin;
	fin = fopen(SML_RAW_FILE, "rb");

	while (1) {
		ssize_t nrd;

		memset(buf, 0x00, sizeof(buf));

		nrd = fread(buf, SML_RAW_LEN, 1, fin);
		if(nrd < 1) break;

		transport_receiver(buf, SML_RAW_LEN);
// weird:
//   libsml: warning: could not read the whole file
//   SML file (1 SML messages, 200 bytes)
//   SML message  101
// same also with  transport_receiver(buf, SML_RAW_LEN+2),  also -2

	}

	fclose(fin);
}


// From https://github.com/volkszaehler/libsml/blob/master/examples/sml_server.c
void transport_receiver(unsigned char *buffer, size_t buffer_len)
{
	int i;
	// github: the buffer contains the whole message, with transport escape sequences.
	// these escape sequences are stripped here.
	//sml_file *file = sml_file_parse(buffer + 8, buffer_len - 16);
	// the sml file is parsed now
	//
	// adapted:
	// buffer contains already de-framed message without escape seqs and without checksum
	sml_file *file = sml_file_parse(buffer, buffer_len);

	// this prints some information about the file
	sml_file_print(file);

	// read here some values ...
	printf("OBIS data\n");
	for (i = 0; i < file->messages_len; i++) {
		sml_message *message = file->messages[i];
		if (*message->message_body->tag == SML_MESSAGE_GET_LIST_RESPONSE) {
			sml_list *entry;
			sml_get_list_response *body;
			body = (sml_get_list_response *) message->message_body->data;
			for (entry = body->val_list; entry != NULL; entry = entry->next) {
				if (!entry->value) { // do not crash on null value
					fprintf(stderr, "Error in data stream. entry->value should not be NULL. Skipping this.\n");
					continue;
				}
				if (entry->value->type == SML_TYPE_OCTET_STRING) {
					char *str;
					printf("%d-%d:%d.%d.%d*%d#%s#\n",
						entry->obj_name->str[0], entry->obj_name->str[1],
						entry->obj_name->str[2], entry->obj_name->str[3],
						entry->obj_name->str[4], entry->obj_name->str[5],
						sml_value_to_strhex(entry->value, &str, true));
					free(str);
				} else if (entry->value->type == SML_TYPE_BOOLEAN) {
					printf("%d-%d:%d.%d.%d*%d#%s#\n",
						entry->obj_name->str[0], entry->obj_name->str[1],
						entry->obj_name->str[2], entry->obj_name->str[3],
						entry->obj_name->str[4], entry->obj_name->str[5],
						entry->value->data.boolean ? "true" : "false");
				} else if (((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_INTEGER) ||
						((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_UNSIGNED)) {
					double value = sml_value_to_double(entry->value);
					int scaler = (entry->scaler) ? *entry->scaler : 0;
					int prec = -scaler;
					if (prec < 0)
						prec = 0;
					value = value * pow(10, scaler);
					printf("%d-%d:%d.%d.%d*%d#%.*f#",
						entry->obj_name->str[0], entry->obj_name->str[1],
						entry->obj_name->str[2], entry->obj_name->str[3],
						entry->obj_name->str[4], entry->obj_name->str[5], prec, value);
					const char *unit = NULL;
					if (entry->unit &&  // do not crash on null (unit is optional)
						(unit = dlms_get_unit((unsigned char) *entry->unit)) != NULL)
						printf("%s", unit);
					printf("\n");
					// flush the stdout puffer, that pipes work without waiting
					fflush(stdout);
				}
			}
		}
	}

	// free the malloc'd memory
	sml_file_free(file);
}
