/*
 * symbol.c
 *
 * This file is part of ktap by Jovi Zhangwei.
 *
 * Copyright (C) 2013 Azat Khuzhin <a3at.mail@gmail.com>.
 *
 * ktap is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * ktap is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <libelf.h>


/**
 * @return v_addr of "LOAD" program header, that have zero offset.
 */
static vaddr_t find_load_address(Elf *elf)
{
	GElf_Phdr header;
	size_t headers;
	vaddr_t address = 0;

	elf_getphdrnum(elf, &headers);
	while (headers-- > 0) {
		gelf_getphdr(elf, headers, &header);
		if (header.p_type != PT_LOAD || header.p_offset != 0)
			continue;

		address = header.p_vaddr;
		break;
	}

	return address;
}

static vaddr_t search_symbol(Elf *elf, const char *symbol)
{
	Elf_Data *elf_data = NULL;
	Elf_Scn *scn = NULL;
	GElf_Sym sym;
	GElf_Shdr shdr;

	vaddr_t load_address = find_load_address(elf);

	if (!load_address)
		return 0;

	while ((scn = elf_nextscn(elf, scn))) {
		int i, symbols;
		char *current_symbol;

		gelf_getshdr(scn, &shdr);

		if (shdr.sh_type != SHT_SYMTAB)
			continue;

		elf_data = elf_getdata(scn, elf_data);
		symbols = shdr.sh_size / shdr.sh_entsize;

		for (i = 0; i < symbols; i++) {
			gelf_getsym(elf_data, i, &sym);

			if (GELF_ST_TYPE(sym.st_info) != STT_FUNC)
				continue;

			current_symbol = elf_strptr(elf, shdr.sh_link, sym.st_name);
			if (!strcmp(current_symbol, symbol))
				return sym.st_value - load_address;
		}
	}

	return 0;
}

vaddr_t find_symbol(const char *exec, const char *symbol)
{
	vaddr_t vaddr = 0;
	Elf *elf;
	int fd;

	if (elf_version(EV_CURRENT) == EV_NONE)
		return vaddr;

	fd = open(exec, O_RDONLY);
	if (fd < 0)
		return vaddr;

	elf = elf_begin(fd, ELF_C_READ, NULL);
	if (elf) {
		vaddr = search_symbol(elf, symbol);
		elf_end(elf);
	}

	close(fd);
	return vaddr;
}

