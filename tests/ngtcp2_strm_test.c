/*
 * ngtcp2
 *
 * Copyright (c) 2018 ngtcp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "ngtcp2_strm_test.h"

#include <stdio.h>

#include "ngtcp2_strm.h"
#include "ngtcp2_test_helper.h"
#include "ngtcp2_vec.h"
#include "ngtcp2_frame_chain.h"

static const MunitTest tests[] = {
  munit_void_test(test_ngtcp2_strm_streamfrq_pop),
  munit_void_test(test_ngtcp2_strm_streamfrq_unacked_offset),
  munit_void_test(test_ngtcp2_strm_streamfrq_unacked_pop),
  munit_void_test(test_ngtcp2_strm_discard_reordered_data),
  munit_test_end(),
};

const MunitSuite strm_suite = {
  .prefix = "/strm",
  .tests = tests,
};

static uint8_t nulldata[1024];

static void setup_strm_streamfrq_fixture(ngtcp2_strm *strm,
                                         ngtcp2_objalloc *frc_objalloc,
                                         const ngtcp2_mem *mem) {
  ngtcp2_frame_chain *frc;
  ngtcp2_vec *data;

  ngtcp2_strm_init(strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 2, frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 2,
  };
  data = frc->fr.stream.data;
  data[0] = (ngtcp2_vec){
    .len = 50,
    .base = nulldata,
  };
  data[1] = (ngtcp2_vec){
    .len = 78,
    .base = nulldata + 50,
  };

  ngtcp2_strm_streamfrq_push(strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 2, frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .offset = 128,
    .datacnt = 2,
  };
  data = frc->fr.stream.data;
  data[0] = (ngtcp2_vec){
    .len = 60,
    .base = nulldata + 128,
  };
  data[1] = (ngtcp2_vec){
    .len = 84,
    .base = nulldata + 128 + 60,
  };

  ngtcp2_strm_streamfrq_push(strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 2, frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .offset = 272,
    .datacnt = 2,
  };
  data = frc->fr.stream.data;
  data[0] = (ngtcp2_vec){
    .len = 127,
    .base = nulldata + 512,
  };
  data[1] = (ngtcp2_vec){
    .len = 1,
    .base = nulldata + 768,
  };

  ngtcp2_strm_streamfrq_push(strm, frc);
}

void test_ngtcp2_strm_streamfrq_pop(void) {
  ngtcp2_strm strm;
  ngtcp2_frame_chain *frc;
  const ngtcp2_mem *mem = ngtcp2_mem_default();
  int rv;
  ngtcp2_vec *data;
  ngtcp2_objalloc frc_objalloc;
  size_t i;
  ngtcp2_ksl_it it;

  ngtcp2_objalloc_init(&frc_objalloc, 1024, mem);

  /* Get first chain */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 128);

  assert_int(0, ==, rv);
  assert_size(2, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(50, ==, data[0].len);
  assert_size(78, ==, data[1].len);
  assert_size(2, ==, ngtcp2_ksl_len(strm.tx.streamfrq));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* Get merged chain */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 272);

  assert_int(0, ==, rv);
  assert_size(2, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(50, ==, data[0].len);
  assert_size(78 + 60 + 84, ==, data[1].len);
  assert_size(1, ==, ngtcp2_ksl_len(strm.tx.streamfrq));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* Get merged chain partially */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 271);

  assert_int(0, ==, rv);
  assert_size(2, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(50, ==, data[0].len);
  assert_size(78 + 60 + 83, ==, data[1].len);
  assert_size(2, ==, ngtcp2_ksl_len(strm.tx.streamfrq));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 128);

  assert_int(0, ==, rv);
  assert_uint64(271, ==, frc->fr.stream.offset);
  assert_size(2, ==, frc->fr.stream.datacnt);
  assert_size(1, ==, frc->fr.stream.data[0].len);
  assert_size(127, ==, frc->fr.stream.data[1].len);
  assert_ptr_equal(nulldata + 50 + 78 + 60 + 83, frc->fr.stream.data[0].base);
  assert_ptr_equal(nulldata + 512, frc->fr.stream.data[1].base);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* Not continuous merge */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 273);

  assert_int(0, ==, rv);
  assert_size(3, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(50, ==, data[0].len);
  assert_size(78 + 60 + 84, ==, data[1].len);
  assert_size(1, ==, data[2].len);
  assert_ptr_equal(nulldata + 512, data[2].base);
  assert_size(1, ==, ngtcp2_ksl_len(strm.tx.streamfrq));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 1024);

  assert_int(0, ==, rv);
  assert_uint64(273, ==, frc->fr.stream.offset);
  assert_size(2, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(126, ==, data[0].len);
  assert_ptr_equal(nulldata + 512 + 1, data[0].base);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* split; continuous */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 129);

  assert_int(0, ==, rv);
  assert_uint64(0, ==, frc->fr.stream.offset);
  assert_size(2, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(50, ==, data[0].len);
  assert_ptr_equal(nulldata, data[0].base);
  assert_size(79, ==, data[1].len);
  assert_ptr_equal(nulldata + 50, data[1].base);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 1024);

  assert_int(0, ==, rv);
  assert_uint64(129, ==, frc->fr.stream.offset);
  assert_size(4, ==, frc->fr.stream.datacnt);

  data = frc->fr.stream.data;

  assert_size(59, ==, data[0].len);
  assert_ptr_equal(nulldata + 129, data[0].base);
  assert_size(84, ==, data[1].len);
  assert_ptr_equal(nulldata + 129 + 59, data[1].base);
  assert_size(127, ==, data[2].len);
  assert_ptr_equal(nulldata + 512, data[2].base);
  assert_size(1, ==, data[3].len);
  assert_ptr_equal(nulldata + 768, data[3].base);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* offset gap */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 11,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .offset = 30,
    .datacnt = 1,
    .data[0] =
      {
        .len = 17,
        .base = nulldata + 30,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 1024);

  assert_int(0, ==, rv);
  assert_size(1, ==, frc->fr.stream.datacnt);
  assert_size(11, ==, frc->fr.stream.data[0].len);
  assert_size(1, ==, ngtcp2_ksl_len(strm.tx.streamfrq));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* fin */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 11,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 0, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .fin = 1,
    .offset = 11,
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 1024);

  assert_int(0, ==, rv);
  assert_true(frc->fr.stream.fin);
  assert_size(1, ==, frc->fr.stream.datacnt);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* left == 0 and there is outstanding data */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 0);

  assert_int(0, ==, rv);
  assert_null(frc);

  ngtcp2_strm_free(&strm);

  /* stream datacnt gets below the allocation threshold */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);
  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 17,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(
    &frc, NGTCP2_FRAME_CHAIN_STREAM_DATACNT_THRES + 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .offset = 17,
    .datacnt = NGTCP2_FRAME_CHAIN_STREAM_DATACNT_THRES + 1,
  };
  data = frc->fr.stream.data;

  for (i = 0; i < frc->fr.stream.datacnt; ++i) {
    data[i] = (ngtcp2_vec){
      .len = 1,
      .base = nulldata,
    };
  }

  ngtcp2_strm_streamfrq_push(&strm, frc);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 18);

  assert_int(0, ==, rv);
  assert_false(frc->fr.stream.fin);
  assert_size(2, ==, frc->fr.stream.datacnt);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);

  it = ngtcp2_ksl_begin(strm.tx.streamfrq);
  frc = ngtcp2_ksl_it_get(&it);

  assert_false(frc->fr.stream.fin);
  assert_uint64(18, ==, frc->fr.stream.offset);
  assert_size(NGTCP2_FRAME_CHAIN_STREAM_DATACNT_THRES, ==,
              frc->fr.stream.datacnt);

  ngtcp2_strm_free(&strm);

  /* left is too small */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 127);

  assert_int(0, ==, rv);
  assert_null(frc);

  ngtcp2_strm_free(&strm);

  ngtcp2_objalloc_free(&frc_objalloc);
}

void test_ngtcp2_strm_streamfrq_unacked_offset(void) {
  ngtcp2_strm strm;
  ngtcp2_frame_chain *frc;
  const ngtcp2_mem *mem = ngtcp2_mem_default();
  ngtcp2_objalloc frc_objalloc;

  ngtcp2_objalloc_init(&frc_objalloc, 1024, mem);

  /* Everything acknowledged including fin */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_FIN_ACKED, 0, 0, NULL,
                   &frc_objalloc, mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 17,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .fin = 1,
    .offset = 443,
    .datacnt = 1,
    .data[0] =
      {
        .len = 971,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 443 + 971);

  assert_uint64((uint64_t)-1, ==, ngtcp2_strm_streamfrq_unacked_offset(&strm));

  ngtcp2_strm_free(&strm);

  /* Everything acknowledged but fin */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 17,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .fin = 1,
    .offset = 443,
    .datacnt = 1,
    .data[0] =
      {
        .len = 971,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 443 + 971);

  assert_uint64(443 + 971, ==, ngtcp2_strm_streamfrq_unacked_offset(&strm));

  ngtcp2_strm_free(&strm);

  /* Unacked gap starts in the middle of stream to resend */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 971,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 443);

  assert_uint64(443, ==, ngtcp2_strm_streamfrq_unacked_offset(&strm));

  ngtcp2_strm_free(&strm);

  /* Unacked gap starts after stream to resend */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .datacnt = 1,
    .data[0] =
      {
        .len = 971,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 971);

  assert_uint64((uint64_t)-1, ==, ngtcp2_strm_streamfrq_unacked_offset(&strm));

  ngtcp2_strm_free(&strm);

  /* Unacked gap and stream overlap and gap starts before stream */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream = (ngtcp2_stream){
    .type = NGTCP2_FRAME_STREAM,
    .offset = 977,
    .datacnt = 1,
    .data[0] =
      {
        .len = 971,
        .base = nulldata,
      },
  };

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 971);

  assert_uint64(977, ==, ngtcp2_strm_streamfrq_unacked_offset(&strm));

  ngtcp2_strm_free(&strm);

  ngtcp2_objalloc_free(&frc_objalloc);
}

void test_ngtcp2_strm_streamfrq_unacked_pop(void) {
  ngtcp2_strm strm;
  ngtcp2_frame_chain *frc;
  const ngtcp2_mem *mem = ngtcp2_mem_default();
  ngtcp2_vec *data;
  int rv;
  ngtcp2_objalloc frc_objalloc;

  ngtcp2_objalloc_init(&frc_objalloc, 1024, mem);

  /* Everything acknowledged including fin */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_FIN_ACKED, 0, 0, NULL,
                   &frc_objalloc, mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream.type = NGTCP2_FRAME_STREAM;
  frc->fr.stream.fin = 0;
  frc->fr.stream.offset = 307;
  frc->fr.stream.datacnt = 1;
  data = frc->fr.stream.data;
  data[0].len = 149;
  data[0].base = nulldata;

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream.type = NGTCP2_FRAME_STREAM;
  frc->fr.stream.fin = 1;
  frc->fr.stream.offset = 457;
  frc->fr.stream.datacnt = 1;
  data = frc->fr.stream.data;
  data[0].len = 307;
  data[0].base = nulldata;

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 764);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 1024);

  assert_int(0, ==, rv);
  assert_null(frc);

  ngtcp2_strm_free(&strm);

  /* Everything acknowledged but fin */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, &frc_objalloc,
                   mem);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream.type = NGTCP2_FRAME_STREAM;
  frc->fr.stream.fin = 0;
  frc->fr.stream.offset = 307;
  frc->fr.stream.datacnt = 1;
  data = frc->fr.stream.data;
  data[0].len = 149;
  data[0].base = nulldata;

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_frame_chain_stream_datacnt_objalloc_new(&frc, 1, &frc_objalloc, mem);
  frc->fr.stream.type = NGTCP2_FRAME_STREAM;
  frc->fr.stream.fin = 1;
  frc->fr.stream.offset = 457;
  frc->fr.stream.datacnt = 1;
  data = frc->fr.stream.data;
  data[0].len = 307;
  data[0].base = nulldata;

  ngtcp2_strm_streamfrq_push(&strm, frc);

  ngtcp2_strm_ack_data(&strm, 0, 764);

  frc = NULL;
  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 1024);

  assert_int(0, ==, rv);
  assert_uint64(NGTCP2_FRAME_STREAM, ==, frc->fr.type);
  assert_true(frc->fr.stream.fin);
  assert_uint64(764, ==, frc->fr.stream.offset);
  assert_uint64(0, ==,
                ngtcp2_vec_len(frc->fr.stream.data, frc->fr.stream.datacnt));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* Remove leading acknowledged data */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  ngtcp2_strm_ack_data(&strm, 0, 12);

  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 140);

  assert_int(0, ==, rv);
  assert_uint64(NGTCP2_FRAME_STREAM, ==, frc->fr.type);
  assert_false(frc->fr.stream.fin);
  assert_uint64(12, ==, frc->fr.stream.offset);
  assert_size(2, ==, frc->fr.stream.datacnt);
  assert_uint64(140, ==,
                ngtcp2_vec_len(frc->fr.stream.data, frc->fr.stream.datacnt));
  assert_ptr_equal(nulldata + 12, frc->fr.stream.data[0].base);

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  /* Creating a gap of acknowledged data */
  setup_strm_streamfrq_fixture(&strm, &frc_objalloc, mem);

  ngtcp2_strm_ack_data(&strm, 130, 1);

  rv = ngtcp2_strm_streamfrq_pop(&strm, &frc, 140);

  assert_int(0, ==, rv);
  assert_uint64(NGTCP2_FRAME_STREAM, ==, frc->fr.type);
  assert_false(frc->fr.stream.fin);
  assert_uint64(0, ==, frc->fr.stream.offset);
  assert_size(2, ==, frc->fr.stream.datacnt);
  assert_uint64(130, ==,
                ngtcp2_vec_len(frc->fr.stream.data, frc->fr.stream.datacnt));

  ngtcp2_frame_chain_objalloc_del(frc, &frc_objalloc, mem);
  ngtcp2_strm_free(&strm);

  ngtcp2_objalloc_free(&frc_objalloc);
}

void test_ngtcp2_strm_discard_reordered_data(void) {
  ngtcp2_strm strm;
  const ngtcp2_mem *mem = ngtcp2_mem_default();

  /* No reordered data has been received. */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, NULL, mem);

  ngtcp2_strm_update_rx_offset(&strm, 1000000007);
  ngtcp2_strm_discard_reordered_data(&strm);

  assert_null(strm.rx.rob);
  assert_uint64(1000000007, ==, ngtcp2_strm_rx_offset(&strm));

  ngtcp2_strm_free(&strm);

  /* Discard reordered data */
  ngtcp2_strm_init(&strm, 0, NGTCP2_STRM_FLAG_NONE, 0, 0, NULL, NULL, mem);

  ngtcp2_strm_update_rx_offset(&strm, 1000000007);
  ngtcp2_strm_recv_reordering(&strm, nulldata, 117, 1000000008);

  assert_not_null(strm.rx.rob);
  assert_uint64(1000000007, ==, ngtcp2_strm_rx_offset(&strm));

  ngtcp2_strm_discard_reordered_data(&strm);

  assert_null(strm.rx.rob);
  assert_uint64(1000000007, ==, ngtcp2_strm_rx_offset(&strm));

  ngtcp2_strm_free(&strm);
}
