 /**
  * Derived from the mbedOS example: https://github.com/ARMmbed/mbed-os-example-ble/tree/development/BLE_GAP
  * See the github repository for the associated copyright information and Apache 2 license.
  * 
  * AUTHOR: 
  * tom@tjpetz.com - 11 April 2021
  * 
  * DESCRIPTION:
  * Updating to support advertising on both legacy and extended (coded) phys.
  *
  * IMPORTANT:  
  * This example is designed to ONLY run on an nRF52840 based board (e.g Arduino Nano 33 BLE or nRF52840 Dev Kit).
  * 
  * This requires using a second advertising set.  Note, contrary to the example you cannot simply
  * add multiple advertising sets in the same call.  Rather you need to add each advertising set
  * via a dispatch call.  If you attempt to add multiple advertising sets without queueing them then
  * only the first set will be active.
  *
  * In this demo we will advertise on both the legacy advertising set and an additional advertising
  * set on the CODED phy.
  *
  * As of 9 May 2021 I believe this is a bug either in mbed OS or the Cordio layer.  As it occurs on 2 different
  * boards it's not board specific.  
  */

#include <mbed.h>
#include <events/mbed_events.h>
#include "ble/BLE.h"
#include "ble/Gap.h"
#include "ble/gap/AdvertisingDataParser.h"

using namespace std::chrono;
using std::milli;
using namespace std::literals::chrono_literals;

static mbed::DigitalOut led1(LED1, 0);

// REDIRECT_STDOUT_TO(Serial);   // Redirect the console to the USBSerial port

events::EventQueue event_queue;


inline void print_error(ble_error_t err, const char* msg) {
  printf("%s: %s", msg, BLE::errorToString(err));
}

/** print device address to the terminal */
inline void print_address(const ble::address_t &addr)
{
    printf("%02x:%02x:%02x:%02x:%02x:%02x\r\n",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

inline void print_mac_address()
{
    /* Print out device MAC address to the console*/
    ble::own_address_type_t addr_type;
    ble::address_t address;
    BLE::Instance().gap().getAddress(addr_type, address);
    printf("DEVICE MAC ADDRESS: ");
    print_address(address);
}

inline const char* phy_to_string(ble::phy_t phy) {
    switch(phy.value()) {
        case ble::phy_t::LE_1M:
            return "LE 1M";
        case ble::phy_t::LE_2M:
            return "LE 2M";
        case ble::phy_t::LE_CODED:
            return "LE coded";
        default:
            return "invalid PHY";
    }
}

void print_ble_features(ble::Gap& gap) {
    printf("BLE Controller Features\n\r");
    printf("\tLE_ENCRYPTION = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_ENCRYPTION) ? "true" : "false");
    printf("\tCONNECTION_PARAMETERS_REQUEST_PROCEDURE = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::CONNECTION_PARAMETERS_REQUEST_PROCEDURE) ? "true" : "false");
    printf("\tEXTENDED_REJECT_INDICATION = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::EXTENDED_REJECT_INDICATION) ? "true" : "false");
    printf("\tSLAVE_INITIATED_FEATURES_EXCHANGE = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::SLAVE_INITIATED_FEATURES_EXCHANGE) ? "true" : "false");
    printf("\tLE_PING = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_PING) ? "true" : "false");
    printf("\tLE_DATA_PACKET_LENGTH_EXTENSION = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_DATA_PACKET_LENGTH_EXTENSION) ? "true" : "false");
    printf("\tLL_PRIVACY = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LL_PRIVACY) ? "true" : "false");
    printf("\tEXTENDED_SCANNER_FILTER_POLICIES = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::EXTENDED_SCANNER_FILTER_POLICIES) ? "true" : "false");
    printf("\tLE_2M_PHY = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_2M_PHY) ? "true" : "false");
    printf("\tSTABLE_MODULATION_INDEX_TRANSMITTER = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::STABLE_MODULATION_INDEX_TRANSMITTER) ? "true" : "false");
    printf("\tSTABLE_MODULATION_INDEX_RECEIVER = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::STABLE_MODULATION_INDEX_RECEIVER) ? "true" : "false");
    printf("\tLE_CODED_PHY = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_CODED_PHY) ? "true" : "false");
    printf("\tLE_EXTENDED_ADVERTISING = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_EXTENDED_ADVERTISING) ? "true" : "false");
    printf("\tLE_PERIODIC_ADVERTISING = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_PERIODIC_ADVERTISING) ? "true" : "false");
    printf("\tCHANNEL_SELECTION_ALGORITHM_2 = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::CHANNEL_SELECTION_ALGORITHM_2) ? "true" : "false");
    printf("\tLE_POWER_CLASS = %s\n\r", gap.isFeatureSupported(ble::controller_supported_features_t::LE_POWER_CLASS) ? "true" : "false");
}


class MultipleAdvertisingSetsDemo : private mbed::NonCopyable<MultipleAdvertisingSetsDemo>, public ble::Gap::EventHandler
{
public:
    MultipleAdvertisingSetsDemo(BLE& ble, events::EventQueue& event_queue) :
        _ble(ble),
        _gap(ble.gap()),
        _event_queue(event_queue)
    {
    }

    ~MultipleAdvertisingSetsDemo()
    {
        if (_ble.hasInitialized()) {
            _ble.shutdown();
        }
    }

    /** Start BLE interface initialisation */
    void run()
    {
        /* handle gap events */
        _gap.setEventHandler(this);

        ble_error_t error = _ble.init(this, &MultipleAdvertisingSetsDemo::on_init_complete);
        if (error) {
            print_error(error, "Error returned by BLE::init");
            return;
        }

        /* this will not return until shutdown */
        _event_queue.dispatch_forever();
    }

private:
    /** This is called when BLE interface is initialised and starts the first mode */
    void on_init_complete(BLE::InitializationCompleteCallbackContext *event)
    {
        if (event->error) {
            print_error(event->error, "Error during the initialisation");
            return;
        }

        print_mac_address();
        printf("Max advertising sets = %d\n", _gap.getMaxAdvertisingSetNumber());
        print_ble_features(_gap);

        /* all calls are serialised on the user thread through the event queue */
        _event_queue.call(this, &MultipleAdvertisingSetsDemo::advertiseLegacy);

        if (_gap.isFeatureSupported(ble::controller_supported_features_t::LE_EXTENDED_ADVERTISING) &&
                _gap.isFeatureSupported(ble::controller_supported_features_t::LE_CODED_PHY)) {     
            _event_queue.call_in(250ms, this, &MultipleAdvertisingSetsDemo::advertiseCoded);
            // _event_queue.call(this, &MultipleAdvertisingSetsDemo::advertiseCoded);
           }
    }

    /** Legacy advertising so everyone can find us. */
    void advertiseLegacy()
    {
        ble_error_t error;
        ble::AdvertisingDataSimpleBuilder<ble::LEGACY_ADVERTISING_MAX_SIZE> data_builder;  

        error = _gap.setAdvertisingParameters(ble::LEGACY_ADVERTISING_HANDLE,
          ble::AdvertisingParameters(
            ble::advertising_type_t::NON_CONNECTABLE_UNDIRECTED,     // We're only ever advertising and we're not connectable
            ble::adv_interval_t(ble::millisecond_t(80)),
            ble::adv_interval_t(ble::millisecond_t(160))
            )
          .setTxPower(4)
          .includeTxPowerInHeader(true)
          );
        if (!error) {
          // Build the advertising payload
          data_builder.setName("Legacy Set").setTxPowerAdvertised(4);
                    
          error = _gap.setAdvertisingPayload(ble::LEGACY_ADVERTISING_HANDLE, data_builder.getAdvertisingData()); 
          if (!error) {
            error = _gap.startAdvertising(ble::LEGACY_ADVERTISING_HANDLE);
            if (!error) {
              printf(
                "\r\nAdvertising started (handle: %d)\r\n",
                ble::LEGACY_ADVERTISING_HANDLE);
            } else {
              print_error(error, "Gap::startAdvertising() failed");
            }           
          } else {
            print_error(error, "Gap::setAdvertisingPayload");
          }       
        } else {
          print_error(error, "Gap::setAdvertisingParameters() failed");
        }
    }

    /** Advertise on the Coded PHY with an additional advertising set. */
    void advertiseCoded() 
    {
      ble_error_t error;
      ble::AdvertisingDataSimpleBuilder<ble::LEGACY_ADVERTISING_MAX_SIZE> data_builder;

      // Setup an additional advertising set.
      error = _gap.createAdvertisingSet(&_coded_adv_handle,
        ble::AdvertisingParameters(
          ble::advertising_type_t::NON_CONNECTABLE_UNDIRECTED,
          ble::adv_interval_t(100),
          ble::adv_interval_t(200),
          false
        )
      );
      if (!error) {
        error = _gap.setAdvertisingParameters(
          _coded_adv_handle,
          ble::AdvertisingParameters()
            .setType(ble::advertising_type_t::NON_CONNECTABLE_UNDIRECTED, false)

            .setPhy(ble::phy_t::LE_CODED, ble::phy_t::LE_CODED)
            .setTxPower(8)
            .includeTxPowerInHeader(true)
            );
        if (!error) {
          data_builder.setName("Coded Set").setTxPowerAdvertised(8);

          error = _gap.setAdvertisingPayload(_coded_adv_handle, data_builder.getAdvertisingData());
          if (!error) {
            error = _gap.startAdvertising(_coded_adv_handle);
            if (!error) {
              printf(
                  "\r\nAdvertising started (handle: %d)\r\n",
                  _coded_adv_handle);
            } else {
              print_error(error, "Advertising the Coded Advertising Set");
            }

          } else {
            print_error(error, "Setting Coded Advertising Payload");
          }
        } else {
          print_error(error, "Setting Advertising Parameters for the Coded set");
        }
      } else {
        print_error(error, "Creating coded advertising set");
      }

    }    
private:
    /* Gap::EventHandler */

    void onScanRequestReceived(const ble::ScanRequestEvent &event) override
    {
      printf("Scan request received from: ");
      print_address(event.getPeerAddress());
    }    

    void onAdvertisingStart(const ble::AdvertisingStartEvent &advHandle) override
    {
      printf("Advertising started on handle: %d\n", advHandle);
    }

    void onAdvertisingEnd(const ble::AdvertisingEndEvent &event) override
    {
      printf("Advertising ended\n");
    }

private:
    BLE &_ble;
    ble::Gap &_gap;
    events::EventQueue &_event_queue;
    ble::advertising_handle_t _coded_adv_handle = ble::INVALID_ADVERTISING_HANDLE;
};

/** Schedule processing of events from the BLE middleware in the event queue. */
void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context)
{
    event_queue.call(mbed::Callback<void()>(&context->ble, &BLE::processEvents));
}

void blink() {
    led1 = (led1 == 1 ? 0 : 1);
}

int main()
{
    BLE &ble = BLE::Instance();

    ThisThread::sleep_for(7s);     // Give us some time after reboot to select the serial port in the Arduino IDE
    
    event_queue.call_every(1s, blink);

    ble.onEventsToProcess(schedule_ble_events);

    MultipleAdvertisingSetsDemo demo(ble, event_queue);

    demo.run();
}

