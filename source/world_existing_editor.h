#ifndef RME_WORLD_EXISTING_EDITOR_H_
#define RME_WORLD_EXISTING_EDITOR_H_
#include "world_editor.h"
#include "world_existing.h"
#include <memory>

namespace WorldEditor {
    struct ExistingOptions {
        GenerationSpec spec;
        wxString profilePath;
        bool addNature=true;
        bool connectPaths=true;
        int protectionRadius=2,maxGap=8,maxConnections=16;
    };
    struct PreparedExisting;
    class ExistingPreview {
    public:
        const WorldExisting::Analysis& analysis() const;
        const ApplicationStats& stats() const;
        const GenerationRegion& region() const;
        uint64_t fingerprint() const;
        size_t changedTiles() const;
        size_t roadTiles() const;
        const std::map<int,size_t>& unknownGrounds() const;
        bool ready() const {return bool(prepared);}
    private:
        std::shared_ptr<PreparedExisting> prepared;
        friend bool PrepareExisting(Editor&,const ExistingOptions&,ExistingPreview&,wxString&,const WorldGen::ProgressCallback&);
        friend bool ApplyExisting(Editor&,const ExistingPreview&,wxString&,ApplicationStats*,const WorldGen::ProgressCallback&);
        friend wxImage RenderExisting(const ExistingPreview&,int);
        friend wxString DescribeExisting(const ExistingPreview&);
        friend bool ExportExisting(const ExistingPreview&,const wxString&,wxString&);
    };
    bool PrepareExisting(Editor& editor,const ExistingOptions& options,ExistingPreview& preview,wxString& error,const WorldGen::ProgressCallback& progress={});
    bool ApplyExisting(Editor& editor,const ExistingPreview& preview,wxString& error,ApplicationStats* stats=nullptr,const WorldGen::ProgressCallback& progress={});
    // 0 recognition, 1 proposed changes, 2 eligible/protected/unknown.
    wxImage RenderExisting(const ExistingPreview& preview,int view=1);
    wxString DescribeExisting(const ExistingPreview& preview);
    bool ExportExisting(const ExistingPreview& preview,const wxString& jsonPath,wxString& error);
}
#endif
