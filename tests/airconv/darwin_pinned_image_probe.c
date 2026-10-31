// Research probe: compare a pre-opened file to the mapped image after dlopen.
// This is not a production identity implementation.
#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/stat.h>
#include <unistd.h>

static bool same_stat(const struct stat *a, const struct stat *b) {
  return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
         a->st_size == b->st_size &&
         a->st_mtimespec.tv_sec == b->st_mtimespec.tv_sec &&
         a->st_mtimespec.tv_nsec == b->st_mtimespec.tv_nsec &&
         a->st_ctimespec.tv_sec == b->st_ctimespec.tv_sec &&
         a->st_ctimespec.tv_nsec == b->st_ctimespec.tv_nsec;
}

static bool hash_file(int fd, unsigned char *digest) {
  CC_SHA256_CTX ctx;
  if (lseek(fd, 0, SEEK_SET) < 0 || !CC_SHA256_Init(&ctx))
    return false;
  unsigned char bytes[65536];
  ssize_t n;
  while ((n = read(fd, bytes, sizeof(bytes))) > 0)
    if (!CC_SHA256_Update(&ctx, bytes, (CC_LONG)n))
      return false;
  return n == 0 && CC_SHA256_Final(digest, &ctx);
}

int main(int argc, char **argv) {
  if (argc != 3)
    return 1;
  int fd = open(argv[1], O_RDONLY);
  struct stat initial, after_hash;
  unsigned char hash[CC_SHA256_DIGEST_LENGTH];
  if (fd < 0 || fstat(fd, &initial) || !hash_file(fd, hash) ||
      fstat(fd, &after_hash) || !same_stat(&initial, &after_hash))
    return 2;
  printf("OPEN ");
  for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; ++i)
    printf("%02x", hash[i]);
  puts("");
  fflush(stdout);
  if (getchar() == EOF)
    return 3;
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    fprintf(stderr, "%s\n", dlerror());
    return 4;
  }
  void *symbol = dlsym(library, argv[2]);
  Dl_info info;
  struct proc_regionwithpathinfo map = {0};
  struct stat loaded;
  if (!symbol || !dladdr(symbol, &info) ||
      proc_pidinfo(getpid(), PROC_PIDREGIONPATHINFO, (uint64_t)info.dli_fbase,
                   &map, sizeof(map)) != (int)sizeof(map) ||
      fstat(fd, &loaded))
    return 5;
  bool bound = same_stat(&initial, &loaded) &&
               (uint32_t)initial.st_dev == map.prp_vip.vip_vi.vi_stat.vst_dev &&
               (uint64_t)initial.st_ino == map.prp_vip.vip_vi.vi_stat.vst_ino;
  printf("BOUND %d\n", bound);
  fflush(stdout);
  if (getchar() == EOF)
    return 6;
  struct stat final, final_after_hash;
  unsigned char final_hash[CC_SHA256_DIGEST_LENGTH];
  bool stable = fstat(fd, &final) == 0 && same_stat(&initial, &final) &&
                hash_file(fd, final_hash) &&
                fstat(fd, &final_after_hash) == 0 &&
                same_stat(&initial, &final_after_hash) &&
                memcmp(hash, final_hash, sizeof(hash)) == 0;
  printf("STABLE %d\n", stable);
  fflush(stdout);
  dlclose(library);
  close(fd);
  return 0;
}
