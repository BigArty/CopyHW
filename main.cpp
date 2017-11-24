//1.41  cp1.02 t10_1.37 t2_4.55 t3_2.44       t3_1.03 t3_1.16
#include <pthread.h>
#include <vector>
#include <stdio.h>
#include <unistd.h>
#include <utime.h>
#include <fcntl.h>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

using namespace std;

struct Tree {
    bool isReady = false;
    bool isFile = false;
    bool isWorking = false;
    bool isCopied = false;
    string src;
    string dst;
    vector<Tree *> list;
};

void printTree(Tree *root) {
    printf("%s\n", root->src.c_str());
    if (!root->isFile) {
        for (int i = 0; i < root->list.size(); ++i) {
            printTree(root->list[i]);
        }
    }
}

void errorArg() {
    printf("Bad arguments!");
    exit(-1);
}

int cpSMT(string src, string dst, bool isFile) {
    //printf("%s\n",src.c_str());
    if (isFile) {
        struct stat srcStat;
        if (lstat(src.c_str(), &srcStat) < 0) {
            string s = "File " + src + " isn't copied";
            perror(s.c_str());
            return 0;
        }
        int fdin = open(src.c_str(), O_RDONLY);
        if (fdin < 0) {
            string s = "File " + src + " isn't copied";
            perror(s.c_str());
            return 0;
        }
        struct stat dstStat;
        if ((lstat(dst.c_str(), &dstStat) == 0)) {
            if (S_ISDIR(dstStat.st_mode)) {
                string s = "File " + src + " isn't copied. Dir with such name is already existed.";
                printf("%s\n",s.c_str());
                close(fdin);
                return 0;
            }
            string reName = dst + ".old";
            if (rename(dst.c_str(), reName.c_str()) != 0) {
                string s = "File " + src + " isn't copied. Can't rename old file";
                perror(s.c_str());
                close(fdin);
                return 0;
            }

        }
        int fdout = open(dst.c_str(), O_WRONLY | O_CREAT);
        if (fdout < 0) {
            string s = "File " + src + " isn't copied. Can't create file";
            perror(s.c_str());
            close(fdin);
            return 0;
        }
        char buf[4096];
        int rd;
        while ((rd = read(fdin, buf, sizeof buf)) > 0) {
            write(fdout, &buf, rd);
        }
        if (chown(dst.c_str(), srcStat.st_uid, srcStat.st_gid) < 0) {
            string s = "Can't change uid or gid in " + dst;
            perror(s.c_str());
            close(fdout);
            close(fdin);
            return 0;
        }
        struct utimbuf time;
        time.actime = srcStat.st_atime;
        time.modtime = srcStat.st_mtime;
        if (utime(dst.c_str(), &time) < 0) {
            string s = "Can't change time's characteristics in " + dst;
            perror(s.c_str());
            close(fdout);
            close(fdin);
            return 0;
        }
        if (chmod(dst.c_str(), srcStat.st_mode) < 0) {
            string s = "Can't change mode in " + dst;
            perror(s.c_str());
        }
        close(fdout);
        close(fdin);
        return 0;
    } else {
        struct stat dirStat;
        if (stat(src.c_str(), &dirStat) != 0) {
            string s = "Dir " + src + " isn't copied";
            perror(s.c_str());
            //exit(2);
            return -1;
        }
        if (access(dst.c_str(), F_OK) == -1) {
            if (mkdir(dst.c_str(), dirStat.st_mode) != 0) {
                string s = "Dir " + src + " isn't copied";
                perror(s.c_str());
                //exit(2);
                return -1;
            }
        }
        else {
            struct stat dstStat;
            if ((lstat(dst.c_str(), &dstStat) != 0)) {
                string s = "Dir " + src + " isn't copied";
                perror(s.c_str());
                //exit(2);
                return -1;
            }
            if (!S_ISDIR(dstStat.st_mode)) {
                string s = "Dir " + src + " isn't copied. File with such name is already existed.";
                printf("%s\n",s.c_str());
                return -1;
            }
        }
        return 0;
    }
}

Tree *root = new Tree();
struct data {
    Tree *root;
    pthread_mutex_t *mutex;
};

void makeTree(string src, string dst, Tree *point) {
    struct stat srcStat;
    string path = src;
    stat(src.c_str(), &srcStat);
    //printf("%s\n",src.c_str());
    point->src = src;
    point->dst = dst;
    if (!S_ISDIR(srcStat.st_mode)) {
        point->isFile = true;

    } else {
        point->isFile = false;
        DIR *direct = opendir(path.c_str());
        for (struct dirent *dir = readdir(direct); dir != nullptr; dir = readdir(direct)) {
            if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0) {
                continue;
            }
            string newSrc = src + '/' + dir->d_name;
            string newDst = dst + '/' + dir->d_name;
            Tree *newList = new Tree();
            point->list.push_back(newList);
            makeTree(newSrc, newDst, newList);
        }
        closedir(direct);
    }
}

bool copy(Tree *point, pthread_mutex_t *mutex) {
    if (point->isCopied) {
        return true;
    }
    if (point->isReady) {
        bool isCopied = true;
        for (int i = 0; i < point->list.size(); ++i) {
            isCopied = isCopied && copy(point->list[i], mutex);
        }
        if (isCopied) {
            point->isCopied = true;
            return true;
        }
        return false;
    }
    if (!point->isWorking) {
        point->isWorking = true;
        pthread_mutex_unlock(mutex);
        int i = cpSMT(point->src, point->dst, point->isFile);
        pthread_mutex_lock(mutex);
        point->isReady = true;
        if (point->isFile) {
            point->isCopied = true;
            return true;
        } else {
            if (i < 0) {
                point->isCopied = true;
                return true;
            }
        };
    }
    return false;
}

void *thread(void *arg) {
    data *d = (data *) arg;
    pthread_mutex_lock(d->mutex);
    //printf("Hi!%i",d->root->isCopied);
    Tree *point = d->root;
    while (!d->root->isCopied) {
        copy(root, d->mutex);
        pthread_mutex_unlock(d->mutex);
        usleep(100);
        pthread_mutex_lock(d->mutex);
    }
    pthread_mutex_unlock(d->mutex);
    pthread_exit(nullptr);
}

int main(int argc, char **argv) {
    if (argc != 4) {
        errorArg();
    }
    int threads;
    if (argv[1][0] == '-' && argv[1][1] == 't') {
        threads = atoi(argv[1] + 2);
        threads = threads > 0 ? threads : 1;
    } else {
        errorArg();
    }
    string src = argv[2];
    string dst = argv[3];
    struct stat dirStat;
    stat(src.c_str(), &dirStat);
    if (access(dst.c_str(), F_OK) == -1) {
        if (mkdir(dst.c_str(), dirStat.st_mode) != 0) {
            string s = "Dir " + src + " isn't copied";
            perror(s.c_str());
            exit(2);
        }
    }
    root->isReady = true;
    makeTree(src, dst, root);
    //printTree(root);
    //printf("\n\n");
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    vector<pthread_t> thrds(threads);
    vector<data> args(threads);
    for (int i = 0; i < threads; i++) {
        args[i].mutex = &mutex;
        args[i].root = root;
    }
    for (int i = 0; i < threads; i++) {
        if (pthread_create(&thrds[i], NULL, thread, &args[i]) != 0) {
            perror("thread create");
            exit(3);
        }
    }
    for (int i = 0; i < threads; i++) {
        pthread_join(thrds[i], NULL);
    }
    //cpSMT("neFile", "test/neFile", false);//test
    return 0;
}