/**
 * \file pcm/pcm_fswitch.c
 * \ingroup PCM_Plugins
 * \brief PCM Fswitch Plugin Interface
 * \author Martin Pietryka <martin.pietryka@streamunlimited.com>
 * \date 2017
 */
/*
 *  PCM - Format based switcher, based on the copy plugin
 *  Copyright (c) 2017 by Martin Pietryka <martin.pietryka@streamunlimited.com>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as
 *   published by the Free Software Foundation; either version 2.1 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include "pcm_local.h"
#include "pcm_plugin.h"

#ifndef PIC
/* entry for static linking */
const char *_snd_module_pcm_fswitch = "";
#endif

#ifndef DOC_HIDDEN
typedef struct {
	char *name;
	snd_pcm_format_t format;
} snd_pcm_fswitch_slave_entry_t;

typedef struct {
	/* This field need to be the first */
	snd_pcm_plugin_t plug;

	snd_pcm_fswitch_slave_entry_t *slaves;
	int slaves_count;
	snd_pcm_stream_t stream;
	int mode;
	int active_slave;
} snd_pcm_fswitch_t;
#endif

static void fswitch_free_slave_table(snd_pcm_fswitch_slave_entry_t *table, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		free(table[i].name);
	}

	free(table);
}

static int snd_pcm_fswitch_hw_refine_cprepare(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params)
{
	int err;
	snd_pcm_access_mask_t access_mask = { SND_PCM_ACCBIT_SHM };
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_ACCESS,
					 &access_mask);
	if (err < 0)
		return err;
	params->info &= ~(SND_PCM_INFO_MMAP | SND_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_pcm_fswitch_hw_refine_sprepare(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *sparams)
{
	snd_pcm_access_mask_t saccess_mask = { SND_PCM_ACCBIT_MMAP };
	_snd_pcm_hw_params_any(sparams);
	_snd_pcm_hw_param_set_mask(sparams, SND_PCM_HW_PARAM_ACCESS,
				   &saccess_mask);
	return 0;
}

static int snd_pcm_fswitch_hw_refine_schange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					     snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = ~SND_PCM_HW_PARBIT_ACCESS;
	err = _snd_pcm_hw_params_refine(sparams, links, params);
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcm_fswitch_hw_refine_cchange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					     snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = ~SND_PCM_HW_PARBIT_ACCESS;
	err = _snd_pcm_hw_params_refine(params, links, sparams);
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcm_fswitch_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return snd_pcm_hw_refine_slave(pcm, params,
				       snd_pcm_fswitch_hw_refine_cprepare,
				       snd_pcm_fswitch_hw_refine_cchange,
				       snd_pcm_fswitch_hw_refine_sprepare,
				       snd_pcm_fswitch_hw_refine_schange,
				       snd_pcm_generic_hw_refine);
}

static int snd_pcm_fswitch_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	int err, new_slave, i;
	snd_pcm_fswitch_t *fswitch = pcm->private_data;
	snd_pcm_t *slave = fswitch->plug.gen.slave;
	snd_pcm_format_t format;

	err = INTERNAL(snd_pcm_hw_params_get_format)(params, &format);
	if (err < 0)
		return err;

	new_slave = 0;
	for (i = 0; i < fswitch->slaves_count; i++) {
		if (fswitch->slaves[i].format == format) {
			new_slave = i;
			break;
		}
	}

	if (new_slave != fswitch->active_slave) {
		snd_pcm_unlink_hw_ptr(pcm, slave);
		snd_pcm_unlink_appl_ptr(pcm, slave);
		snd_pcm_close(slave);


		err = snd_pcm_open(&slave, fswitch->slaves[new_slave].name, fswitch->stream, fswitch->mode);
		if (err < 0)
			return err;

		fswitch->plug.gen.slave = slave;
		pcm->poll_fd = slave->poll_fd;
		pcm->poll_events = slave->poll_events;
		pcm->tstamp_type = slave->tstamp_type;

		snd_pcm_link_hw_ptr(pcm, slave);
		snd_pcm_link_appl_ptr(pcm, slave);

		snd_pcm_set_hw_ptr(pcm, &fswitch->plug.hw_ptr, -1, 0);
		snd_pcm_set_appl_ptr(pcm, &fswitch->plug.appl_ptr, -1, 0);

		fswitch->active_slave = new_slave;
	}

	return snd_pcm_hw_params_slave(pcm, params,
				       snd_pcm_fswitch_hw_refine_cchange,
				       snd_pcm_fswitch_hw_refine_sprepare,
				       snd_pcm_fswitch_hw_refine_schange,
				       snd_pcm_generic_hw_params);
}

static snd_pcm_uframes_t
snd_pcm_fswitch_write_areas(snd_pcm_t *pcm,
			    const snd_pcm_channel_area_t *areas,
			    snd_pcm_uframes_t offset,
			    snd_pcm_uframes_t size,
			    const snd_pcm_channel_area_t *slave_areas,
			    snd_pcm_uframes_t slave_offset,
			    snd_pcm_uframes_t *slave_sizep)
{
	if (size > *slave_sizep)
		size = *slave_sizep;
	snd_pcm_areas_copy(slave_areas, slave_offset,
			   areas, offset,
			   pcm->channels, size, pcm->format);
	*slave_sizep = size;
	return size;
}

static snd_pcm_uframes_t
snd_pcm_fswitch_read_areas(snd_pcm_t *pcm,
			   const snd_pcm_channel_area_t *areas,
			   snd_pcm_uframes_t offset,
			   snd_pcm_uframes_t size,
			   const snd_pcm_channel_area_t *slave_areas,
			   snd_pcm_uframes_t slave_offset,
			   snd_pcm_uframes_t *slave_sizep)
{
	if (size > *slave_sizep)
		size = *slave_sizep;
	snd_pcm_areas_copy(areas, offset, 
			   slave_areas, slave_offset,
			   pcm->channels, size, pcm->format);
	*slave_sizep = size;
	return size;
}

static int snd_pcm_fswitch_close(snd_pcm_t *pcm)
{
	snd_pcm_fswitch_t *fswitch = pcm->private_data;

	fswitch_free_slave_table(fswitch->slaves, fswitch->slaves_count);

	return snd_pcm_generic_close(pcm);
}

static void snd_pcm_fswitch_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_fswitch_t *fswitch = pcm->private_data;
	snd_output_printf(out, "Format based switcher PCM\n");
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
	/* TODO: show list of slaves and shot active slave */
	snd_output_printf(out, "Slave: ");
	snd_pcm_dump(fswitch->plug.gen.slave, out);
}

static const snd_pcm_ops_t snd_pcm_fswitch_ops = {
	.close = snd_pcm_fswitch_close,
	.info = snd_pcm_generic_info,
	.hw_refine = snd_pcm_fswitch_hw_refine,
	.hw_params = snd_pcm_fswitch_hw_params,
	.hw_free = snd_pcm_generic_hw_free,
	.sw_params = snd_pcm_generic_sw_params,
	.channel_info = snd_pcm_generic_channel_info,
	.dump = snd_pcm_fswitch_dump,
	.nonblock = snd_pcm_generic_nonblock,
	.async = snd_pcm_generic_async,
	.mmap = snd_pcm_generic_mmap,
	.munmap = snd_pcm_generic_munmap,
	.query_chmaps = snd_pcm_generic_query_chmaps,
	.get_chmap = snd_pcm_generic_get_chmap,
	.set_chmap = snd_pcm_generic_set_chmap,
};

/**
 * \brief Creates a new fswitch PCM
 * \param pcmp Returns created PCM handle
 * \param name Name of PCM
 * \param slave Slave PCM handle
 * \param close_slave When set, the slave PCM handle is closed with fswitch PCM
 * \retval zero on success otherwise a negative error code
 * \warning Using of this function might be dangerous in the sense
 *          of compatibility reasons. The prototype might be freely
 *          changed in future.
 */
int snd_pcm_fswitch_open(snd_pcm_t **pcmp, const char *name, snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_fswitch_t *fswitch;
	int err;
	assert(pcmp && slave);
	fswitch = calloc(1, sizeof(snd_pcm_fswitch_t));
	if (!fswitch) {
		return -ENOMEM;
	}
	snd_pcm_plugin_init(&fswitch->plug);
	fswitch->plug.read = snd_pcm_fswitch_read_areas;
	fswitch->plug.write = snd_pcm_fswitch_write_areas;
	fswitch->plug.undo_read = snd_pcm_plugin_undo_read_generic;
	fswitch->plug.undo_write = snd_pcm_plugin_undo_write_generic;
	fswitch->plug.gen.slave = slave;
	fswitch->plug.gen.close_slave = close_slave;

	err = snd_pcm_new(&pcm, SND_PCM_TYPE_FSWITCH, name, slave->stream, slave->mode);
	if (err < 0) {
		free(fswitch);
		return err;
	}
	pcm->ops = &snd_pcm_fswitch_ops;
	pcm->fast_ops = &snd_pcm_plugin_fast_ops;
	pcm->private_data = fswitch;
	pcm->poll_fd = slave->poll_fd;
	pcm->poll_events = slave->poll_events;
	pcm->tstamp_type = slave->tstamp_type;
	snd_pcm_set_hw_ptr(pcm, &fswitch->plug.hw_ptr, -1, 0);
	snd_pcm_set_appl_ptr(pcm, &fswitch->plug.appl_ptr, -1, 0);
	*pcmp = pcm;

	return 0;
}

static snd_pcm_format_t fswitch_get_format(snd_config_t *conf)
{
	snd_config_iterator_t i, next;

	assert(conf);

	snd_config_for_each(i, next, conf) {
		const char *id, *fmts;
		snd_config_t *n = snd_config_iterator_entry(i);

		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (snd_config_get_type(n) == SND_CONFIG_TYPE_STRING && strcmp(id, "format") == 0) {
			int err;
			err = snd_config_get_string(n, &fmts);

			if (err < 0) {
				SNDERR("Invalid type for %s\n", id);
				return SND_PCM_FORMAT_UNKNOWN;
			}

			return snd_pcm_format_value(fmts);
		}
	}
	return SND_PCM_FORMAT_UNKNOWN;
}

static char *fswitch_get_pcm_name(snd_config_t *conf)
{
	char *ret;
	snd_config_iterator_t i, next;

	assert(conf);

	snd_config_for_each(i, next, conf) {
		const char *id, *pcm_name;
		snd_config_t *n = snd_config_iterator_entry(i);

		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (snd_config_get_type(n) == SND_CONFIG_TYPE_STRING && strcmp(id, "pcm") == 0) {
			int err;
			err = snd_config_get_string(n, &pcm_name);

			if (err < 0) {
				SNDERR("Invalid type for %s\n", id);
				return NULL;
			}

			ret = malloc(strlen(pcm_name));
			if (ret == NULL)
				return NULL;

			strcpy(ret, pcm_name);

			return ret;
		}
	}
	return NULL;
}


/*! \page pcm_plugins

\section pcm_plugins_fswitch Plugin: fswitch

This plying routes audio to a slave PCM based on the format. The
slave will be opened when the format is known and other slaves
will be closed.

On startup the first slave in the list will be opened.

\code
pcm.name {
	type fswitch		# Format switcher PCM

	slaves {
		ID {
			pcm STR		# Slave PCM name
			[format STR]	# Format definition
		}
	}
}
\endcode

\subsection pcm_plugins_fswitch_funcref Function reference

<UL>
  <LI>snd_pcm_fswitch_open()
  <LI>_snd_pcm_fswitch_open()
</UL>

*/

/**
 * \brief Creates a new fswitch PCM
 * \param pcmp Returns created PCM handle
 * \param name Name of PCM
 * \param root Root configuration node
 * \param conf Configuration node with fswitch PCM description
 * \param stream Stream type
 * \param mode Stream mode
 * \retval zero on success otherwise a negative error code
 * \warning Using of this function might be dangerous in the sense
 *          of compatibility reasons. The prototype might be freely
 *          changed in future.
 */
int _snd_pcm_fswitch_open(snd_pcm_t **pcmp, const char *name,
		       snd_config_t *root, snd_config_t *conf, 
		       snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	int err;
	snd_pcm_t *spcm;

	snd_config_t *slaves = NULL, *sconf;
	int slaves_count = 0, j;
	snd_pcm_fswitch_slave_entry_t *slave_table;

	if (stream != SND_PCM_STREAM_PLAYBACK) {
		SNDERR("The fswitch plugin supports only playback streams");
		return -EINVAL;
	}

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (snd_pcm_conf_generic_id(id))
			continue;
		if (strcmp(id, "slaves") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			slaves = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slaves) {
		SNDERR("slaves is not defined");
		return -EINVAL;
	}

	snd_config_for_each(i, next, slaves) {
		++slaves_count;
	}

	slave_table = calloc(slaves_count, sizeof(snd_pcm_fswitch_slave_entry_t));
	if (!slave_table) {
		return -ENOMEM;
	}

	j = 0;
	snd_config_for_each(i, next, slaves) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (snd_pcm_conf_generic_id(id))
			continue;

		slave_table[j].name = fswitch_get_pcm_name(n);
		slave_table[j].format = fswitch_get_format(n);
		j++;
	}

	err = snd_pcm_open(&spcm, "null", stream, mode);
	if (err < 0) {
		fswitch_free_slave_table(slave_table, slaves_count);
		return err;
	}

	err = snd_pcm_fswitch_open(pcmp, name, spcm, 1);
	if (err < 0) {
		snd_pcm_close(spcm);
		fswitch_free_slave_table(slave_table, slaves_count);
		return err;
	} else {
		((snd_pcm_fswitch_t*) (*pcmp)->private_data)->slaves = slave_table;
		((snd_pcm_fswitch_t*) (*pcmp)->private_data)->slaves_count = slaves_count;
		((snd_pcm_fswitch_t*) (*pcmp)->private_data)->stream = stream;
		((snd_pcm_fswitch_t*) (*pcmp)->private_data)->mode = mode;
		((snd_pcm_fswitch_t*) (*pcmp)->private_data)->active_slave = -1;
	}

	return err;
}
#ifndef DOC_HIDDEN
SND_DLSYM_BUILD_VERSION(_snd_pcm_fswitch_open, SND_PCM_DLSYM_VERSION);
#endif
