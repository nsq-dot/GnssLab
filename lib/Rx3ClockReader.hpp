
#ifndef Rx3ClockReader_HPP
#define Rx3ClockReader_HPP

#include <iomanip>
#include <map>
#include "Exception.h"
#include "GnssStruct.h"
#include <fstream>

    class Rx3ClockReader {
    public:

        // Constructor.
        Rx3ClockReader()
                : firstLine(true) {
            satClockData.clear();
        };

        void read(const std::string &filename) {
            satClockData.clear();
            // open the input stream
            std::fstream strm(filename.c_str(), ios::in);
            if (!strm.is_open()) {
                FileMissingException e("File " + filename + " could not be opened");
                throw (e);
            }
            readHeader(strm);
            readData(strm);
            // close
            strm.close();
        };


        virtual void readHeader(std::fstream &strm)
        noexcept(false);

        virtual void readData(std::fstream &strm)
        noexcept(false);

        // Destructor
        virtual ~Rx3ClockReader() {};

        //>>>>> header

        static const std::string versionString;         //< "RINEX VERSION / TYPE"
        static const std::string runByString;           //< "PGM / RUN BY / DATE"
        static const std::string intervalString;        //< "INTERVAL"
        static const std::string commentString;         //< "COMMENT"
        static const std::string sysString;             //< "SYS / # / OBS TYPES"
        static const std::string timeSystemString;      //< "TIME SYSTEM ID"
        static const std::string leapSecondsString;     //< "LEAP SECONDS"
        static const std::string leapSecondsGNSSString; //< "LEAP SECONDS GNSS"
        static const std::string sysDCBString;          //< "SYS / DCBS APPLIED"
        static const std::string sysPCVString;          //< "SYS / PCVS APPLIED"
        static const std::string numDataString;         //< "# / TYPES OF DATA"
        static const std::string stationNameString;     //< "STATION NAME / NUM"
        static const std::string stationClockRefString; //< "STATION CLK REF"
        static const std::string analysisCenterString;  //< "ANALYSIS CENTER"
        static const std::string numClockRefString;     //< "# OF CLK REF"
        static const std::string analysisClkRefrString; //< "ANALYSIS CLK REF"
        static const std::string numReceiversString;    //< "# OF SOLN STA / TRF"
        static const std::string solnStateString;       //< "SOLN STA NAME / NUM"
        static const std::string numSolnSatsString;     //< "# OF SOLN SATS"
        static const std::string prnListString;         //< "PRN LIST"
        static const std::string endOfHeaderString;     //< "END OF HEADER"


        double version;                        //< Rinex3Clock Version or file format
        std::string program;                   //< Program name
        std::string runby;                     //< Run by string
        double interval;                       //< Interval
        std::vector<std::string> dataTypes;    //< list of data types
        int leapSeconds;                       //< Leap seconds
        TimeSystem timeSystem;                 //< Time system

        std::string analCenterDesignator;      //< Analysis center designator (3 char)
        std::string analysisCenter;            //< Analysis center
        std::string terrRefFrame;              //< Terr Ref Frame or SINEX solution
        SatID pcvsSystem;                      //< system (G=GPS, R=GLO) for PCVs
        std::string pcvsProgram;               //< program used to apply PCVs
        std::string pcvsSource;                //< source of applied PCVs

        int numSolnStations;                   //< Number of stations in the solution
        std::map<std::string, std::string> stationID; //< 4-char name, station id

        // these coordinates are often more than 32 bits -- cannot store as number!
        std::map<std::string, std::string> stationX;  //< name, station X coord in mm
        std::map<std::string, std::string> stationY;  //< name, station Y coord in mm
        std::map<std::string, std::string> stationZ;  //< name, station Z coord in mm

        int numSolnSatellites;                  ///< Number of satellites in the soln
        std::vector<SatID> satList;        ///< List of sats (PRN LIST)

        std::vector<std::string> commentList;   ///< comments

        unsigned long valid;                    ///< valid bits for this header

        // read first line to judge the version
        bool firstLine;

        //>>>>>> data

        std::string datatype;   //< Data type : AR, AS, etc
        SatID sat;         //< Satellite ID        (if AS)
        std::string site;       //< Site label (4-char) (if AR)
        CommonTime time;        //< Time of epoch for this record
        double bias;            //< Clock bias in seconds
        double sig_bias;        //< Clock bias sigma in seconds
        double drift;           //< Clock drift in sec/sec
        double sig_drift;       //< Clock drift sigma in sec/sec
        double accel;           //< Clock acceleration in 1/sec
        double sig_accel;       //< Clock acceleration sigma in 1/sec


        // return data
        std::map<SatID, std::map<CommonTime, double>> satClockData;

    };



#endif // Rx3ClockReader_HPP
