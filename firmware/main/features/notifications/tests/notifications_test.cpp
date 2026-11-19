#include "../flow.hpp"
#include "../inbox.hpp"
#include "../store.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using namespace mosaic::features::notifications;

namespace {
Notification make(std::string_view id, std::string_view title = "Build failed on main") {
    Notification n;
    copy_text(n.id, id);
    copy_text(n.source, "GitHub");
    copy_text(n.title, title);
    n.occurred_at = 1'795'000'000;
    return n;
}

void the_field_rules() {
    assert(valid(make("n-0001")));
    // No text is fine.
    assert(make("n-0001").text_view().empty());
    // Empty or unprintable IDs, an empty source or title, and no time are not.
    assert(!valid(make("")));
    assert(!valid(make("tab\tid")));
    assert(!valid(make("caf\xC3\xA9")));
    Notification no_source = make("n-1");
    no_source.source = { };
    assert(!valid(no_source));
    assert(!valid(make("n-1", "")));
    Notification no_time = make("n-1");
    no_time.occurred_at = 0;
    assert(!valid(no_time));
    // A field that doesn't end within its array.
    Notification unterminated = make("n-1");
    unterminated.source.fill('x');
    assert(!valid(unterminated));
    // The longest ID: 63 characters.
    assert(valid(make(std::string(63, 'a'))));
}

void long_text_is_cut_at_a_character() {
    Notification n;
    // "é" is two bytes; a cut through it backs off to before it.
    copy_text(n.source, std::string(30, 'a') + "\xC3\xA9");
    assert(n.source_view() == std::string(30, 'a'));
    copy_text(n.source, std::string(40, 'b'));
    assert(n.source_view() == std::string(31, 'b'));
}

void the_inbox_keeps_new_ones_and_spots_resends() {
    Inbox inbox;
    assert(!inbox.latest() && inbox.arrivals() == 0);
    assert(inbox.accept(make("n-1")) == Receipt::stored);
    assert(inbox.latest()->id_view() == "n-1" && inbox.arrivals() == 1);
    // A resend after a reconnect changes nothing.
    assert(inbox.accept(make("n-1", "Changed")) == Receipt::duplicate);
    assert(inbox.latest()->title_view() == "Build failed on main" && inbox.arrivals() == 1);
    // Invalid ones are never kept.
    assert(inbox.accept(make("")) == Receipt::invalid);
    assert(inbox.arrivals() == 1);
    assert(inbox.accept(make("n-2")) == Receipt::stored);
    assert(inbox.latest()->id_view() == "n-2" && inbox.arrivals() == 2);
}

void a_full_inbox_forgets_the_oldest() {
    Inbox inbox;
    for (size_t i = 0; i < Inbox::kCapacity; ++i)
        assert(inbox.accept(make("n-" + std::to_string(i))) == Receipt::stored);
    assert(inbox.accept(make("n-0")) == Receipt::duplicate);
    assert(inbox.accept(make("n-new")) == Receipt::stored);
    // n-0 made room, so it counts as new again; the rest are still known.
    assert(inbox.accept(make("n-1")) == Receipt::duplicate);
    assert(inbox.accept(make("n-0")) == Receipt::stored);
    assert(inbox.latest()->id_view() == "n-0");
}
void the_store_shares_the_newest() {
    Notification out;
    uint32_t arrivals = 99;
    assert(!store::latest(out, arrivals) && arrivals == 0);
    assert(store::accept(make("s-1")) == Receipt::stored);
    assert(store::accept(make("s-1")) == Receipt::duplicate);
    assert(store::latest(out, arrivals) && arrivals == 1 && out.id_view() == "s-1");
}

void incoming_then_detail_or_dismissed() {
    using View = Flow::View;
    Flow flow;
    assert(flow.view() == View::none && !flow.open_detail());
    flow.arrived(1'000);
    assert(flow.view() == View::incoming);
    // View opens the detail, which stays however long it's open.
    assert(flow.open_detail() && flow.view() == View::detail);
    assert(!flow.open_detail());
    assert(!flow.tick(1'000 + 10 * Flow::kIncomingMs) && flow.view() == View::detail);
    // Back from the detail.
    flow.dismiss();
    assert(flow.view() == View::none);
    // Not now.
    flow.arrived(2'000);
    flow.dismiss();
    assert(flow.view() == View::none && !flow.tick(1'000'000));
}

void an_untouched_notification_times_out() {
    Flow flow;
    flow.arrived(5'000);
    assert(!flow.tick(5'000 + Flow::kIncomingMs - 1));
    assert(flow.tick(5'000 + Flow::kIncomingMs) && flow.view() == Flow::View::none);
    // Only once.
    assert(!flow.tick(5'000 + Flow::kIncomingMs + 1));
    // Across the clock wrapping around.
    flow.arrived(0xFFFF'F000);
    assert(!flow.tick(0x0000'0100));
    assert(flow.tick(0xFFFF'F000 + Flow::kIncomingMs));
}

void the_newest_replaces_what_is_open() {
    Flow flow;
    flow.arrived(0);
    flow.open_detail();
    // Another arrives while reading: it shows, and gets its own full time.
    flow.arrived(20'000);
    assert(flow.view() == Flow::View::incoming);
    assert(!flow.tick(20'000 + Flow::kIncomingMs - 1));
    flow.arrived(40'000);
    assert(!flow.tick(20'000 + Flow::kIncomingMs));
    assert(flow.tick(40'000 + Flow::kIncomingMs));
}
} // namespace

int main() {
    the_field_rules();
    long_text_is_cut_at_a_character();
    the_inbox_keeps_new_ones_and_spots_resends();
    a_full_inbox_forgets_the_oldest();
    the_store_shares_the_newest();
    incoming_then_detail_or_dismissed();
    an_untouched_notification_times_out();
    the_newest_replaces_what_is_open();
    std::puts("Notification tests passed");
}
