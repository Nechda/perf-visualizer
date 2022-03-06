#! /usr/bin/python3
from PIL import Image
from matplotlib import rcParams
import matplotlib.pyplot as plt
import numpy as np

import preprosess
import argparse

class Heatmap:

    def gen_image(self, filename):
        rgb_array = np.uint8(255 * self.data)
        rgb_array = np.flipud(rgb_array)
        img = Image.fromarray(rgb_array)
        img.save(filename)

    def fill_heatmap(self, data, index = 0):
        for item in data:
            pid, ip, time = item
            y = self.table_ip_to_page[ip]
            x = int(time / self.total_time * self.max_time_index)
            x = min(self.max_time_index - 1, x)
            self.data[y][x][index] += 1
                
    def normalization(self):
        self.data = np.log10(1 + self.data)
        maxes = np.amax(self.data, axis = (0, 1))
        self.data = self.data / maxes

    def create_ip_to_page_table(self, data):
        addreses = list(set(key[1] for key in data))
        addreses.sort()
        pages = list(set(addr // 4096 for addr in addreses))
        pages.sort()
        pages = dict((pages[i],i) for i in range(len(pages)))

        self.table_ip_to_page = dict((addr, pages[addr // 4096]) for addr in addreses)
        self.max_addr_index = len(pages)
        self.sorted_addreses = addreses
        print("Total pages = ", self.max_addr_index)

    def __init__(self, inst, icache, itlb, out_file):

        self.out_file = out_file

        #Read info from files and create structures
        preprosess.get_adress_info(inst, icache, itlb)
        self.total_time = preprosess.total_time
        self.used_records = preprosess.used_records
        self.mmap_table = preprosess.mmap_table
        data_sets = preprosess.data_sets

        #Create table for converting .ip into page index
        self.create_ip_to_page_table(sum(data_sets, []))
        
        #defining image sizes:
        self.x_cells = 512
        self.y_cells = self.max_addr_index
        self.max_time_index = self.x_cells
        
        #filling data into heatmap
        self.data = np.zeros((self.max_addr_index, self.max_time_index,3), 'float64')
        self.fill_heatmap(data_sets[0],2)
        self.fill_heatmap(data_sets[1],1)
        self.fill_heatmap(data_sets[2],0)
        self.normalization()

    def __gen_time_ticks(self, img_width, total_time, n = 10):
        print(f'{total_time = }')
        img_width -= 1
        n += 1
        ind = np.mgrid[0:img_width:complex(0,n)]
        labels = np.rint(np.mgrid[0:total_time:complex(0,n)])
        return ind, labels

    def __gen_addr_ticks(self, img_height_pxls, plot_height_inch):
        factor = plot_height_inch / img_height_pxls
        fontsize_inch = rcParams['ytick.labelsize'] / 72

        ind = []
        labels = []
        cell_text = []
        #sorting mmap_regions by .addr filed
        self.used_records = list(self.used_records)
        self.used_records.sort(key = lambda item: self.mmap_table[item][1])

        for id in self.used_records:
            pid, addr, length, name = self.mmap_table[id]
            gater = -1
            set_of_used_pages = set()
            for ptr in self.sorted_addreses:
                if addr <= ptr <= addr + length - 1:
                    gater = ptr if gater == -1 else gater
                    set_of_used_pages.add(self.table_ip_to_page[ptr])

            if gater == -1:
                continue
            ind.append(self.table_ip_to_page[gater])

            label_text = '{}'.format(len(cell_text)+1)
            if len(ind) > 2:
                delta = ind[-1] - ind[-2]
                #can we insert letter?
                if delta * factor < fontsize_inch:
                    label_text = ''
                    if len(labels[-1]):
                        labels[-1] = labels[-1] + '...'
            labels.append(label_text)

            #extract only filename instead of full path
            name = name[name.rfind('/')+1:]
            cell_text.append([len(cell_text)+1, hex(addr), length//4096, len(set_of_used_pages), name])

        return ind, labels, cell_text

    def show_image(self, filename):
        __TICK_LABEL_FONT_SIZE__ = 10
        #For better location
        rcParams['font.family'] = 'monospace'
        rcParams['figure.figsize'] = [16, 9]
        rcParams['xtick.labelsize'] = __TICK_LABEL_FONT_SIZE__
        rcParams['ytick.labelsize'] = __TICK_LABEL_FONT_SIZE__

        fig, axs = plt.subplots(1,2)
        fig.tight_layout()
        ax = axs[0]

        #Read just generated image
        img = Image.open(filename)
        _, im_height = img.size
        img = img.resize((im_height,im_height))
        a = np.flipud(np.asarray(img))
        ax.imshow(a, origin = 'lower', interpolation='nearest')

        #Settings some labels
        ax.set_title(
            'Total time:{} ms; Total time (no kernel):{} ms\n'
            'Total records:{}; Kernel:{}; User:{}'
            .format(
                preprosess.actual_total_time,
                preprosess.total_time,
                preprosess.total_records,
                preprosess.kernel_records,
                preprosess.total_records - preprosess.kernel_records
            )
        )
        ax.set_xlabel("Time, ms")
        ax.set_ylabel("Address")

        #Create ticks for time axis
        ind, labels = self.__gen_time_ticks(im_height, self.total_time)
        ax.set_xticks(ind)
        ax.set_xticklabels(labels)

        #Create ticks for addr axis
        bbox = ax.get_window_extent().transformed(fig.dpi_scale_trans.inverted())
        ind, labels, cell_text = self.__gen_addr_ticks(im_height, bbox.height)
        ax.set_yticks(ind)
        ax.set_yticklabels(labels)

        #Move to table settings
        ax = axs[1]
        ax.axis('off')
        table = ax.table(
            cellText  = cell_text,
            colLabels = ['Ref','Addr','Length (Pages)', 'Used', 'Path'],
            loc       = 'center',
            cellLoc   = 'right'
        )
        #table.auto_set_font_size(False)
        table.set_fontsize(10)
        table.auto_set_column_width([0,1,2,3,4])
        
        #plt.show()
        fig.savefig(self.out_file, bbox_inches = 'tight', dpi = 300)


parser = argparse.ArgumentParser(description='Building graphs from perf data.')
parser.add_argument('-o', type=str, default='plot.png',
	            help='Output plot file.')
args = parser.parse_args()
hm = Heatmap("data_inst.txt", "data_icache.txt", "data_itlb.txt", args.o)
hm.gen_image(args.o)
hm.gen_image('raw_'+args.o)
hm.show_image(args.o)

