// SCTE-35 splice manipulator TSDuck plugin.
#include "tsPluginRepository.h"
#include "tsBinaryTable.h"
#include "tsSectionDemux.h"
#include "tsSpliceInformationTable.h"
#include "tsSpliceInsert.h"
#include "tsSpliceSegmentationDescriptor.h"
#include "tsReporterBase.h"
#include "tsSectionProviderInterface.h"
#include "tsPacketizer.h"
#include "tsjsonObject.h"
#include "tsObject.h"
#include <algorithm>
#include <queue>
#include <vector>

//----------------------------------------------------------------------------
// Plugin definition
//----------------------------------------------------------------------------

namespace ts {
    class SpliceManipulatorPlugin:
        public ProcessorPlugin,
        private TableHandlerInterface,
        private SectionProviderInterface
    {
        TS_PLUGIN_CONSTRUCTORS(SpliceManipulatorPlugin);
    public:
        // Implementation of plugin API
        virtual bool getOptions() override;
        virtual bool start() override;
        virtual bool stop() override;
        virtual Status processPacket(TSPacket&, TSPacketMetadata&) override;

    private:
        // SectionDemux stuff
        PID              _splice_pid = PID_NULL;   // The only splice PID to monitor.
        SectionDemux     _section_demux {duck, this}; // Section filter for splice information.
 
        // Packetizer stuff
        Packetizer             _packetizer {duck, PID_NULL, this};  // Packetizer for Splice Information sections.
        std::queue<SectionPtr> _pendingSections;

        // Implementation of TableHandlerInterface
        virtual void handleTable(SectionDemux&, const BinaryTable&) override;

        // Implementation of SectionProviderInterface.
        virtual void provideSection(SectionCounter counter, SectionPtr& section) override;
        virtual bool doStuffing() override;

        // Custom manipulations for Scte35
        virtual void handleScte35SpliceInsert(SpliceInformationTable& sit);
        virtual void handleScte35TimeSignal(SpliceInformationTable& sit);        
    
        // Segmentation Descriptors map
        std::map<uint8_t, uint8_t> _manipulation_rules;
        std::vector<uint8_t> _filter_rules;
        UString _in_file {};
        virtual bool loadRules(const UString& filename);
        virtual bool loadManipulationRules(const json::Value& json);
        virtual bool loadFilterRules(const json::Value& json);

        const UString RULE_KEY   = u"manipulate";
        const UString FILTER_KEY = u"filter";
        const UString USAGE      = u"tsp -I <input> -P splicemanipulator --splice-pid <PID> --rules <FILENAME_JSON> -O Drop";
        const UString EXAMPLE_JSON = u"{\"manipulate\":[{\"in\":48,\"out\":34},{\"in\":49,\"out\":35}],\"filter\":[{\"command\":4},{\"command\":5}]}";
    };
}

TS_REGISTER_PROCESSOR_PLUGIN(u"splicemanipulator", ts::SpliceManipulatorPlugin);


//----------------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------------

ts::SpliceManipulatorPlugin::SpliceManipulatorPlugin(TSP* tsp_) :
    ProcessorPlugin(tsp_, u"Manipulate Scte35 packets", u"[options]")
{
    setIntro(
        u"This plugin manipulates and/or filters Scte35 messages from incoming input.\n\n"
        u"- Filters are applied at Scte35 command_type level.\n(e.g: if you want to remove incoming SpliceNull command, filter by command_type=0)\n\n"
        u"- Manipulations are applied ONLY to translate Scte35 TimeSignal Descriptor segmentation_type_id.\n(e.g: translate segmentation_type_id from AdvertisementProviderStart to BreakStart)\n"
        u"TimeSignal with only ONE DESCRIPTOR are supported.\n\n"
        u"Usage: To activate the manipulation and/or filter rules a you need to provide a rules.json file with --rules <FILENAME_JSON>.\n"
        u"\n"
        u"Example:\n" +
        USAGE +
        u"\n\nwhere:\n"
        u"- <PID>: splice pid\n"
        u"- <FILENAME_JSON>: json file with the definition of the manipulation/filter rules\n(e.g: " + EXAMPLE_JSON + ")\n"
        u"- Description:\n"
        u"Filters out incoming SpliceSchedule, SpliceInsert command. Translate TimeSignals descriptors segmentation_type_id from AdvertisementProviderStart/End to Break Start/End\n"
    );

    option(u"splice-pid", 's', PIDVAL);
    help(u"splice-pid",
         u"Specify one PID carrying SCTE-35 sections to monitor.");
    option(u"rules", 'r', FILENAME);
    help(u"rules", u"JSON file containing the definition of the rules to perform Manipulation/Filter on incoming Scte35 messages. e.g:\n", EXAMPLE_JSON);
}


//----------------------------------------------------------------------------
// Get command line options
//----------------------------------------------------------------------------

bool ts::SpliceManipulatorPlugin::getOptions()
{
    getIntValue(_splice_pid, u"splice-pid", PID_NULL);
    getValue(_in_file, u"rules");
    return true;
}


//----------------------------------------------------------------------------
// Start method
//----------------------------------------------------------------------------

bool ts::SpliceManipulatorPlugin::start() 
{
    // Cleanup state
    _section_demux.reset();
    _section_demux.setPIDFilter(NoPID());

    // Start
    if (_splice_pid == PID_NULL) {
        error(u"No --splice-pid option provided. Please run the plugin with --splice-pid <PID>");
        return false;
    }
    _section_demux.addPID(_splice_pid);

    _packetizer.reset();
    _packetizer.setPID(_splice_pid);

    std::queue<SectionPtr> empty;
    _pendingSections.swap(empty);

    // Parse
    if (_in_file.empty()) {
        warning(u"No input JSON file provided for Manipulation/Filter rules. Use the plugin --rules <JSON_FILEPATH> to apply the rules.");
        warning(u"SKIPPING any Scte35 Manipulation/Filter operations");
    }
    else {
        return loadRules(_in_file);        
    }

    return true;
}


//----------------------------------------------------------------------------
// Stop method
//----------------------------------------------------------------------------
bool ts::SpliceManipulatorPlugin::stop() 
{
    return true;
}

//----------------------------------------------------------------------------
// Packet processing method
//----------------------------------------------------------------------------

ts::ProcessorPlugin::Status ts::SpliceManipulatorPlugin::processPacket(TSPacket& pkt, TSPacketMetadata& pktData)
{
    _section_demux.feedPacket(pkt);

    if (pkt.getPID() == _splice_pid) 
    {
        // remove original scte35        
        pkt = NullPacket; 
    }

    if (pkt.getPID() == PID_NULL)
    {
        // send scte35
        _packetizer.getNextPacket(pkt);
    }

    return TSP_OK;
}

//----------------------------------------------------------------------------
// Invoked by the demux when a splice information section is available.
//----------------------------------------------------------------------------

void ts::SpliceManipulatorPlugin::handleTable(SectionDemux& demux, const BinaryTable& table)
{
    // Convert to a Splice Information Table.
    SpliceInformationTable sit(duck, table);
    if (!sit.isValid()){
        return;
    }

    // Dump Scte35 Hex
    if (table.sectionCount() > 0) {
        const ts::SectionPtr sec(table.sectionAt(0));
        info(u"Detected INPUT Scte35: %s",ts::UString::Dump(sec->content(), sec->size()));
    }

    // --------------------
    // APPLY MANIPULATIONS
    // --------------------
    if (!_manipulation_rules.empty())
    {
        // Time Signals
        if (sit.splice_command_type == SPLICE_TIME_SIGNAL && sit.time_signal.has_value()) {
            ts::SpliceManipulatorPlugin::handleScte35TimeSignal(sit);
        }
        // Splice Inserts
        else if (sit.splice_command_type == SPLICE_INSERT) {
            ts::SpliceManipulatorPlugin::handleScte35SpliceInsert(sit);
        }
        else {
            // Splice Nulls, Splice Schedule and others
            // do nothing...
        }
    }

    // --------------------
    // APPLY FILTERS
    // --------------------
    if (std::find(_filter_rules.begin(), _filter_rules.end(), uint8_t(sit.splice_command_type)) != _filter_rules.end())
    {
        info(u"Filtering Scte35 command_type %d", sit.splice_command_type);        
    }
    else
    {
        BinaryTable _out;
        sit.serialize(duck, _out);
        SectionPtr _section = _out.sectionAt(0);
        _pendingSections.push(_section);
        info(u"Sending OUTPUT Scte35: %s",
            ts::UString::Dump(_section->content(), _section->size()));
    }
}

//----------------------------------------------------------------------------
// Invoked when a new splice information section is required.
// Implementation of SectionProviderInterface.
//----------------------------------------------------------------------------
void ts::SpliceManipulatorPlugin::provideSection(SectionCounter counter, SectionPtr& section)
{
    if (_pendingSections.empty()){
        section.reset();
    }
    else {
        section = _pendingSections.front();
        _pendingSections.pop();
    }
}


//----------------------------------------------------------------------------
// Shall we perform section stuffing.
// Implementation of SectionProviderInterface.
//----------------------------------------------------------------------------
bool ts::SpliceManipulatorPlugin::doStuffing()
{
    // Splice Information Table are rare and mostly contained in one or two
    // TS packets. We always stuff to the end of packets after a section.
    return true;
}

//----------------------------------------------------------------------------
// Splice Insert manipulations
//----------------------------------------------------------------------------
void ts::SpliceManipulatorPlugin::handleScte35SpliceInsert(SpliceInformationTable& sit){
    SpliceInsert si(sit.splice_insert);    
    verbose(u"Scte35 SpliceInsert:"
        u", command_type=%d"
        u", event_id=%n"
        u", out_of_network=%d",
        int(sit.splice_command_type),
        si.event_id,
        si.splice_out);
}

//----------------------------------------------------------------------------
// Time Signals manipulations
//----------------------------------------------------------------------------
void ts::SpliceManipulatorPlugin::handleScte35TimeSignal(SpliceInformationTable& sit)
{
    SpliceTime st(sit.time_signal);

    if (sit.descs.size() != 1) {
        warning(u"Detected TimeSignal with %d Segmentation descriptors. Only one is supported",
        sit.descs.size());
        return;
    }

    // first descriptor
    Descriptor& desc = sit.descs[0];
    if (desc.tag() != DID_SPLICE_SEGMENT ) {
        warning(u"Wrong Segmentation descriptor tag: %d", desc.tag());
        return;
    }
    SpliceSegmentationDescriptor ssd(duck, desc);
    if (!ssd.isValid() || !(ssd.isIn() || ssd.isOut())) {
        warning(u"Invalid Segmentation descriptor detected. Skipping operation.");
        return;
    }

    verbose(u"Scte35 TimeSignal:"
        u", command_type=%d"
        u", segmentation_event_id=%n"
        u", segmentation_type_id=0x%x",
        int(sit.splice_command_type),
        ssd.segmentation_event_id,
        ssd.segmentation_type_id);

    // MANIPULATE Segmentation type id
    const auto rule = _manipulation_rules.find(ssd.segmentation_type_id);
    if (rule != _manipulation_rules.end()) {
        uint8_t _segmentation_type_id = rule->second;
        info (u"Scte35 TimeSignal: MATCHING Manipulation rules. Manipulating Segmentation Descriptor Type Id  0x%x ->  0x%x",
            int(ssd.segmentation_type_id),
            int(_segmentation_type_id)
        );
        ssd.segmentation_type_id = _segmentation_type_id;
    }

    Descriptor new_desc;
    ssd.serialize(duck, new_desc);
    sit.descs[0] = new_desc;
}

//----------------------------------------------------------------------------
// Load Rules files
//----------------------------------------------------------------------------
bool ts::SpliceManipulatorPlugin::loadRules(const UString& fileName)
{
    _manipulation_rules.clear();
    _filter_rules.clear();
    json::ValuePtr root;
    if (!json::LoadFile(root, fileName)) {
        error(u"Loading JSON rule file %s", fileName);
        return false;
    }

    if (root == nullptr || !root->isObject()) {
        error(u"Invalid JSON content - expecting to be an object");
        return false;
    }

    const json::Value& manipulation_rules(root->value(RULE_KEY));
    const json::Value& filter_rules(root->value(FILTER_KEY));
    bool result = true;
    if (!manipulation_rules.isNull())
    {
        verbose(u"Loading manipulations from JSON file %s", fileName);
        result &= loadManipulationRules(manipulation_rules);
    }
    if (!filter_rules.isNull())
    {
        verbose(u"Loading filters from JSON file %s", fileName);
        result &= loadFilterRules(filter_rules);
    }
    return result;
}

bool ts::SpliceManipulatorPlugin::loadManipulationRules(const json::Value& json)
{
    if (!json.isArray()) {
        error(u"\"%s\" is not a valid JSON array", RULE_KEY);
        return false;
    }

    for (size_t i = 0; i < json.size(); ++i)
    {
        const json::Value& rule(json.at(i));
        if (!rule.isObject()) {
            warning(u"Manipulation rule %d is not an object", int(i));
            continue;
        }

        const json::Value& inputValue(rule.value(u"in"));
        const json::Value& outputValue(rule.value(u"out"));
        if (!inputValue.isInteger()) {
            warning(u"Manipulation rule %d has no integer segmentation_id input", int(i));
            continue;
        }
        if (!outputValue.isInteger()) {
            warning(u"Manipulation rule %d has no integer segmentation_id output", int(i));
            continue;
        }

        const int64_t input = inputValue.toInteger();
        const int64_t output = outputValue.toInteger();
        if (input < 0 || input > UINT8_MAX || output < 0 || output > UINT8_MAX) {
            warning(u"Manipulation rule %d values must be between 0 and 255", int(i));
            continue;
        }

        _manipulation_rules[uint8_t(input)] = uint8_t(output);

        info(u"Loaded manipulation rule for TimeSignals segmentation_type_id:  0x%x -> 0x%x",
                int(input),
                int(output));
    }
    verbose(u"Loaded %d manipulation rules", int(_manipulation_rules.size()));
    return true;
}

bool ts::SpliceManipulatorPlugin::loadFilterRules(const json::Value& json)
{
    if (!json.isArray()) {
        error(u"\"%s\" is not a valid JSON array", FILTER_KEY);
        return false;
    }

    for (size_t i = 0; i < json.size(); ++i)
    {
        const json::Value& rule(json.at(i));
        if (!rule.isObject()) {
            warning(u"Filter rule %d is not an object", int(i));
            continue;
        }

        const json::Value& inputValue(rule.value(u"command"));
        if (!inputValue.isInteger()) {
            warning(u"Filter rule %d has no integer command input", int(i));
            continue;
        }

        const int64_t input = inputValue.toInteger();
        if (input < 0 || input > UINT8_MAX) {
            warning(u"Filter rule %d command value must be between 0 and 255", int(i));
            continue;
        }
        _filter_rules.push_back(uint8_t(input));

        info(u"Loaded filter rule for command_type 0x%x",int(input));
    }

    verbose(u"Loaded %d filter rules", int(_filter_rules.size()));
    return true;
}
