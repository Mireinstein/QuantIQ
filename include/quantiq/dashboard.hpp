#pragma once

#include <string>

namespace quantiq {

/// Renders the journal as one self-contained HTML file: no server, no runtime
/// dependency, nothing to keep in sync. The journal is the only source of
/// truth, so the page can be thrown away and regenerated at any time, and a
/// crash mid-session costs nothing but the current bar.
///
/// Charts are inline SVG rather than a charting library, because a page that
/// fetches a script from a CDN stops working the moment it is opened without a
/// network -- which is most of the times you actually want to read it.
void write_dashboard(const std::string& journal_path, const std::string& out_path);

/// Uploads the page to a pre-signed blob URL, which is how it reaches a public
/// static website without an Azure SDK or a CLI in the image. The URL is the
/// only credential, so it belongs in the environment rather than on a command
/// line where it would land in shell history and process listings.
void publish_dashboard(const std::string& html_path, const std::string& sas_url);

}  // namespace quantiq
