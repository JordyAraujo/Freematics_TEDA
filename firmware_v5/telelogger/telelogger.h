#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <math.h>

#define WINDOW_THRESHOLD 1

class TEDACompress;

class TEDACompress
{
private:
    int window_threshold = WINDOW_THRESHOLD;
    float k = 1;
    float variance = 0;
    float eccentricity = 0;
    float mean = 0;
    int window_count = 0;
    int time = 0;
    float norm_eccentricity, outlier_threshold, last_value;

public:
    virtual void resetWindow(float x)
    {
        k = 1;
        variance = 0;
        mean = 0;
        window_count = 0;
        last_value = x;
    }
    virtual float calcMean(float x)
    {
        float new_mean = (((k - 1) / k) * mean) + ((1 / k) * x);
        Serial.print("new_mean = ");
        Serial.println(new_mean);
        return new_mean;
    }
    virtual float calcVariance(float x)
    {
        float distance_squared = ((x - mean) * (x - mean));
        float new_var = (((k - 1) / k) * variance) + (distance_squared * (1 / (k - 1)));
        Serial.print("new_var = ");
        Serial.println(new_var);
        return new_var;
    }
    virtual float calcEccentricity(float x)
    {
        float new_ecc;
        float mean2 = (mean - x) * (mean - x);
        if (mean2 == 0)
        {
            new_ecc = 0;
        }
        else
        {
            new_ecc = (1 / k) + ((mean2) / (k * variance));
        }
        Serial.print("new_ecc = ");
        Serial.println(new_ecc);
        return new_ecc;
    }
    virtual bool eval_point(float x)
    {

        int n = 1;
        time = time + 1;

        if (k == 1)
        {
            // Serial.println(k);
            mean = x;
            variance = 0;
            k = k + 1;
            last_value = x;
            if (time == 1)
                return true;
            return false;
        }
        else if (x == last_value && variance == 0)
        {
            mean = calcMean(x);
            variance = calcVariance(x);

            k = k + 1;
            last_value = x;
            return false;
        }
        else

        {
            mean = calcMean(x);
            variance = calcVariance(x);
            eccentricity = calcEccentricity(x);
            norm_eccentricity = eccentricity / 2;

            outlier_threshold = ((n * n) + 1) / (2 * k);

            if (norm_eccentricity > outlier_threshold)
            {
                window_count += 1;
            }
            if (window_count >= window_threshold)
            {
                resetWindow(x);
                return true;
            }
            else
            {
                k = k + 1;
                last_value = x;
                return false;
            }
        }
    };
};

class CStorage;

class CStorage
{
public:
    virtual bool init() { return true; }
    virtual void uninit() {}

    virtual void log(char *name) //Used to print the table header
    {
        char buf[1000];
        byte len = sprintf(buf, "%s:", name);
        dispatch(buf, len);
    }

    virtual void log(uint16_t pid, int value)
    {
        char buf[24];
        byte len = sprintf(buf, "%d:", value);
        dispatch(buf, len);
    }
    virtual void log(uint16_t pid, uint32_t value)
    {
        char buf[24];
        byte len = sprintf(buf, "%u:", value);
        dispatch(buf, len);
    }
    virtual void log(uint16_t pid, float value[])
    {
        char buf[48];
        byte len = sprintf(buf, "%.2f;%.2f;%.2f:", value[0], value[1], value[2]);
        dispatch(buf, len);
    }
    virtual void log(uint16_t pid, float value)
    {
        char buf[48];
        byte len = sprintf(buf, "%f:", value);
        dispatch(buf, len);
    }
    virtual void timestamp(uint32_t ts) //Prints timestamp with the format h:m:s:ms. Also creates a new table line
    {
        char buf[24];
        uint32_t t = ts;
        long h = t / 3600000;
        t = t % 3600000;
        int m = t / 60000;
        t = t % 60000;
        int s = t / 1000;
        int ms = t % 1000;
        byte len = sprintf(buf, "\n%02ldh %02dm %02ds %02dms:", h, m, s, ms);
        dispatch(buf, len);
    }
    virtual void purge() { m_samples = 0; }
    virtual uint16_t samples() { return m_samples; }
    virtual void dispatch(const char *buf, byte len)
    {
        // output data via serial
        Serial.write((uint8_t *)buf, len);
        Serial.write(' ');
        m_samples++;
    }

protected:
    byte checksum(const char *data, int len)
    {
        byte sum = 0;
        for (int i = 0; i < len; i++)
            sum += data[i];
        return sum;
    }
    virtual void header(const char *devid) {}
    virtual void tailer() {}
    uint16_t m_samples = 0;
    char m_delimiter = ':';
};

class CStorageRAM : public CStorage
{
public:
    bool init(unsigned int cacheSize)
    {
        if (m_cacheSize != cacheSize)
        {
            uninit();
            m_cache = new char[m_cacheSize = cacheSize];
        }
        return true;
    }
    void uninit()
    {
        if (m_cache)
        {
            delete m_cache;
            m_cache = 0;
            m_cacheSize = 0;
        }
    }
    void purge()
    {
        m_cacheBytes = 0;
        m_samples = 0;
    }
    unsigned int length() { return m_cacheBytes; }
    char *buffer() { return m_cache; }
    void dispatch(const char *buf, byte len)
    {
        // reserve some space for checksum
        int remain = m_cacheSize - m_cacheBytes - len - 3;
        if (remain < 0)
        {
            // m_cache full
            return;
        }
        // store data in m_cache
        memcpy(m_cache + m_cacheBytes, buf, len);
        m_cacheBytes += len;
        m_cache[m_cacheBytes++] = ',';
        m_samples++;
    }

    void header(const char *devid)
    {
        m_cacheBytes = sprintf(m_cache, "%s#", devid);
    }
    void tailer()
    {
        if (m_cache[m_cacheBytes - 1] == ',')
            m_cacheBytes--;
        m_cacheBytes += sprintf(m_cache + m_cacheBytes, "*%X", (unsigned int)checksum(m_cache, m_cacheBytes));
    }
    void untailer()
    {
        char *p = strrchr(m_cache, '*');
        if (p)
        {
            *p = ',';
            m_cacheBytes = p + 1 - m_cache;
        }
    }

protected:
    unsigned int m_cacheSize = 0;
    unsigned int m_cacheBytes = 0;
    char *m_cache = 0;
};

class FileLogger : public CStorage
{
public:
    FileLogger() { m_delimiter = ','; }
    virtual void dispatch(const char *buf, byte len)
    {
        if (m_id == 0)
            return;

        if (m_file.write((uint8_t *)buf, len) != len)
        {
            // try again
            if (m_file.write((uint8_t *)buf, len) != len)
            {
                Serial.println("Error writing. End file logging.");
                end();
                return;
            }
        }
        //m_file.write('\n');
        m_size += (len + 1);
    }
    virtual uint32_t size()
    {
        return m_size;
    }
    virtual void end()
    {
        m_file.close();
        m_id = 0;
        m_size = 0;
    }
    virtual void flush()
    {
        m_file.flush();
    }

protected:
    int getFileID(File &root)
    {
        if (root)
        {
            File file;
            int id = 0;
            while (file = root.openNextFile())
            {
                Serial.println(file.name());
                if (!strncmp(file.name(), "/DATA/", 6))
                {
                    unsigned int n = atoi(file.name() + 6);
                    if (n > id)
                        id = n;
                }
            }
            return id + 1;
        }
        else
        {
            return 1;
        }
    }
    uint32_t m_dataTime = 0;
    uint32_t m_dataCount = 0;
    uint32_t m_size = 0;
    uint32_t m_id = 0;
    File m_file;
};

class SDLogger : public FileLogger
{
public:
    bool init()
    {
        SPI.begin();
        if (SD.begin(PIN_SD_CS, SPI, SPI_FREQ))
        {
            unsigned int total = SD.totalBytes() >> 20;
            unsigned int used = SD.usedBytes() >> 20;
            Serial.print("SD:");
            Serial.print(total);
            Serial.print(" MB total, ");
            Serial.print(used);
            Serial.println(" MB used");
            return true;
        }
        else
        {
            Serial.println("NO SD CARD");
            return false;
        }
    }
    uint32_t begin()
    {
        File root = SD.open("/DATA");
        m_id = getFileID(root);
        SD.mkdir("/DATA");
        char path[24];
        sprintf(path, "/DATA/%u.CSV", m_id);
        Serial.print("File: ");
        Serial.println(path);
        m_file = SD.open(path, FILE_WRITE);
        if (!m_file)
        {
            Serial.println("File error");
            m_id = 0;
        }
        m_dataCount = 0;
        return m_id;
    }
    void flush()
    {
        char path[24];
        sprintf(path, "/DATA/%u.CSV", m_id);
        m_file.close();
        m_file = SD.open(path, FILE_APPEND);
        if (!m_file)
        {
            Serial.println("File error");
        }
    }
};

class SPIFFSLogger : public FileLogger
{
public:
    bool init()
    {
        bool mounted = SPIFFS.begin();
        if (!mounted)
        {
            Serial.println("Formatting SPIFFS...");
            mounted = SPIFFS.begin(true);
        }
        if (mounted)
        {
            Serial.print("SPIFFS:");
            Serial.print(SPIFFS.totalBytes());
            Serial.print(" bytes total, ");
            Serial.print(SPIFFS.usedBytes());
            Serial.println(" bytes used");
        }
        else
        {
            Serial.println("No SPIFFS");
        }
        return mounted;
    }
    uint32_t begin()
    {
        File root = SPIFFS.open("/");
        m_id = getFileID(root);
        char path[24];
        sprintf(path, "/DATA/%u.CSV", m_id);
        Serial.print("File: ");
        Serial.println(path);
        m_file = SPIFFS.open(path, FILE_WRITE);
        if (!m_file)
        {
            Serial.println("File error");
            m_id = 0;
        }
        m_dataCount = 0;
        return m_id;
    }

private:
    void purge()
    {
        // remove oldest file when unused space is insufficient
        File root = SPIFFS.open("/");
        File file;
        int idx = 0;
        while (file = root.openNextFile())
        {
            if (!strncmp(file.name(), "/DATA/", 6))
            {
                unsigned int n = atoi(file.name() + 6);
                if (n != 0 && (idx == 0 || n < idx))
                    idx = n;
            }
        }
        if (idx)
        {
            m_file.close();
            char path[32];
            sprintf(path, "/DATA/%u.CSV", idx);
            SPIFFS.remove(path);
            Serial.print(path);
            Serial.println(" removed");
            sprintf(path, "/DATA/%u.CSV", m_id);
            m_file = SPIFFS.open(path, FILE_APPEND);
            if (!m_file)
                m_id = 0;
        }
    }
};
