/*
 * demulti2.h
 * 
 * Copyright 2016 0p1pp1
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */

#ifndef __DEMULTI2_H__
#define __DEMULTI2_H__

void *multi2_get_key_struct(void);
void multi2_free_key_struct(void *keys);

void multi2_odd_key_set(const unsigned char *odd, void *keys);
void multi2_even_key_set(const unsigned char *even, void *keys);

int multi2_decrypt_packet(void *keys, unsigned char *packet);

#endif /* __DEMULTI2_H__ */
